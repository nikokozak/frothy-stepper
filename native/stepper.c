/*
 * frothy-stepper-gpl 0.1.0
 *
 * Copyright (C) 2009-2025 Mike McCauley
 * Copyright (C) 2026 Niko Kozak
 *
 * This is a modified C translation of the DRIVER-mode motion algorithm in
 * AccelStepper 1.66. It uses caller-owned Frothy cells, bounded integer
 * arithmetic, and Frothy's checked platform functions. Coil sequencing,
 * MultiStepper, blocking calls, callbacks, enable pins, pin inversion, and
 * configurable pulse width are omitted.
 *
 * Upstream: https://www.airspayce.com/mikem/arduino/AccelStepper/
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3. It comes
 * without any warranty. See LICENSE.
 */

#include "object.h"
#include "platform.h"
#include "runtime.h"
#include "tagged.h"
#include "types.h"

#include <stdbool.h>
#include <stdint.h>

#if !FR_FEATURE_CELLS
#error "frothy-stepper-gpl requires cells"
#endif

typedef char fr_stepper_requires_32_bit_tagged_words
    [(FR_TAGGED_WORD_BYTES == 4u && FR_TAGGED_INT_MAX == 1073741823) ? 1 : -1];

enum {
  FR_STEPPER_CURRENT_POSITION = 0,
  FR_STEPPER_TARGET_POSITION = 1,
  FR_STEPPER_SPEED = 2,
  FR_STEPPER_MAX_SPEED = 3,
  FR_STEPPER_ACCELERATION = 4,
  FR_STEPPER_STEP_INTERVAL = 5,
  FR_STEPPER_LAST_STEP_TIME = 6,
  FR_STEPPER_N = 7,
  FR_STEPPER_C0 = 8,
  FR_STEPPER_CN = 9,
  FR_STEPPER_CMIN = 10,
  FR_STEPPER_DIRECTION = 11,
  FR_STEPPER_STEP_PIN = 12,
  FR_STEPPER_DIRECTION_PIN = 13,
  FR_STEPPER_RESERVED = 14,
  FR_STEPPER_SENTINEL = 15,
  FR_STEPPER_CELL_COUNT = 16,

  FR_STEPPER_READY = 0x535450,
  FR_STEPPER_MIN_PULSE_US = 2,
  FR_STEPPER_MAX_CONFIGURED_SPEED = 46340,
  FR_STEPPER_MAX_CONFIGURED_ACCELERATION = 100000,
};

typedef struct fr_stepper_motor_t {
  fr_object_id_t object_id;
  fr_int_t cells[FR_STEPPER_CELL_COUNT];
} fr_stepper_motor_t;

static fr_err_t fr_stepper_check_call(fr_runtime_t *runtime,
                                      const fr_tagged_t *args,
                                      uint8_t arg_count, uint8_t expected,
                                      fr_tagged_t *out) {
  if (runtime == NULL || out == NULL || arg_count != expected ||
      (args == NULL && arg_count > 0)) {
    return FR_ERR_INVALID;
  }
  return FR_OK;
}

static fr_err_t fr_stepper_decode_int(const fr_tagged_t *args, uint8_t index,
                                      fr_int_t *out) {
  if (args == NULL || out == NULL) {
    return FR_ERR_INVALID;
  }
  return fr_tagged_decode_int(args[index], out);
}

static fr_err_t fr_stepper_object(fr_runtime_t *runtime, fr_tagged_t tagged,
                                  fr_object_id_t *out_object_id) {
  uint16_t length = 0;

  if (runtime == NULL || out_object_id == NULL) {
    return FR_ERR_INVALID;
  }
  FR_TRY(fr_tagged_decode_object_id(tagged, out_object_id));
  FR_TRY(fr_cells_length(runtime, *out_object_id, &length));
  if (length != FR_STEPPER_CELL_COUNT) {
    return FR_ERR_DOMAIN;
  }
  return FR_OK;
}

static fr_err_t fr_stepper_load(fr_runtime_t *runtime, fr_tagged_t tagged,
                                fr_stepper_motor_t *out_motor) {
  fr_tagged_t cell = 0;

  if (out_motor == NULL) {
    return FR_ERR_INVALID;
  }
  FR_TRY(fr_stepper_object(runtime, tagged, &out_motor->object_id));
  for (uint16_t i = 0; i < FR_STEPPER_CELL_COUNT; i++) {
    FR_TRY(fr_cells_read(runtime, out_motor->object_id, i, &cell));
    FR_TRY(fr_tagged_decode_int(cell, &out_motor->cells[i]));
  }

  if (out_motor->cells[FR_STEPPER_SENTINEL] != FR_STEPPER_READY) {
    return FR_ERR_INVALID;
  }
  if (out_motor->cells[FR_STEPPER_MAX_SPEED] < 1 ||
      out_motor->cells[FR_STEPPER_MAX_SPEED] >
          FR_STEPPER_MAX_CONFIGURED_SPEED ||
      out_motor->cells[FR_STEPPER_ACCELERATION] < 1 ||
      out_motor->cells[FR_STEPPER_ACCELERATION] >
          FR_STEPPER_MAX_CONFIGURED_ACCELERATION ||
      out_motor->cells[FR_STEPPER_STEP_INTERVAL] < 0 ||
      out_motor->cells[FR_STEPPER_LAST_STEP_TIME] < 0 ||
      out_motor->cells[FR_STEPPER_C0] <= 0 ||
      out_motor->cells[FR_STEPPER_CN] <= 0 ||
      out_motor->cells[FR_STEPPER_CMIN] <= 0 ||
      (out_motor->cells[FR_STEPPER_DIRECTION] != -1 &&
       out_motor->cells[FR_STEPPER_DIRECTION] != 1) ||
      out_motor->cells[FR_STEPPER_STEP_PIN] < 0 ||
      out_motor->cells[FR_STEPPER_STEP_PIN] > UINT16_MAX ||
      out_motor->cells[FR_STEPPER_DIRECTION_PIN] < 0 ||
      out_motor->cells[FR_STEPPER_DIRECTION_PIN] > UINT16_MAX ||
      out_motor->cells[FR_STEPPER_STEP_PIN] ==
          out_motor->cells[FR_STEPPER_DIRECTION_PIN]) {
    return FR_ERR_DOMAIN;
  }
  return FR_OK;
}

static fr_err_t fr_stepper_write(fr_runtime_t *runtime,
                                 const fr_stepper_motor_t *motor,
                                 uint16_t index, fr_int_t value) {
  fr_tagged_t tagged = 0;

  FR_TRY(fr_tagged_encode_int(value, &tagged));
  return fr_cells_write(runtime, motor->object_id, index, tagged);
}

static fr_err_t fr_stepper_i32(int64_t value, fr_int_t *out) {
  if (out == NULL) {
    return FR_ERR_INVALID;
  }
  if (value < FR_TAGGED_INT_MIN || value > FR_TAGGED_INT_MAX) {
    return FR_ERR_RANGE;
  }
  *out = (fr_int_t)value;
  return FR_OK;
}

static uint32_t fr_stepper_isqrt(uint32_t value) {
  uint32_t result = 0;
  uint32_t bit = 1u << 30;

  while (bit > value) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

static fr_err_t fr_stepper_c0(fr_int_t acceleration, fr_int_t *out) {
  const int64_t numerator = INT64_C(95600800) * 1000;
  uint32_t root100 = 0;
  int64_t c0 = 0;

  if (acceleration < 1 ||
      acceleration > FR_STEPPER_MAX_CONFIGURED_ACCELERATION) {
    return FR_ERR_DOMAIN;
  }
  root100 = fr_stepper_isqrt((uint32_t)acceleration * 10000u);
  c0 = (numerator + root100 / 2u) / root100;
  return fr_stepper_i32(c0, out);
}

static fr_err_t fr_stepper_cmin(fr_int_t speed, fr_int_t *out) {
  int64_t cmin = 0;

  if (speed < 1 || speed > FR_STEPPER_MAX_CONFIGURED_SPEED) {
    return FR_ERR_DOMAIN;
  }
  cmin = (INT64_C(1000000000) + speed - 1) / speed;
  return fr_stepper_i32(cmin, out);
}

static int64_t fr_stepper_divide_nearest(int64_t numerator,
                                         int64_t denominator) {
  bool negative = (numerator < 0) != (denominator < 0);
  uint64_t magnitude =
      numerator < 0 ? (uint64_t)-numerator : (uint64_t)numerator;
  uint64_t divisor =
      denominator < 0 ? (uint64_t)-denominator : (uint64_t)denominator;
  int64_t quotient = (int64_t)((magnitude + divisor / 2u) / divisor);

  return negative ? -quotient : quotient;
}

static int64_t fr_stepper_distance(const fr_stepper_motor_t *motor) {
  return (int64_t)motor->cells[FR_STEPPER_TARGET_POSITION] -
         motor->cells[FR_STEPPER_CURRENT_POSITION];
}

static int64_t fr_stepper_steps_to_stop(const fr_stepper_motor_t *motor) {
  int64_t speed = motor->cells[FR_STEPPER_SPEED];

  return speed * speed / (2 * (int64_t)motor->cells[FR_STEPPER_ACCELERATION]);
}

static fr_err_t fr_stepper_compute_speed(fr_stepper_motor_t *motor) {
  int64_t distance = fr_stepper_distance(motor);
  int64_t steps_to_stop = fr_stepper_steps_to_stop(motor);
  int64_t n = motor->cells[FR_STEPPER_N];
  int64_t cn = motor->cells[FR_STEPPER_CN];
  int64_t denominator = 0;
  int64_t correction = 0;
  int64_t interval = 0;
  int64_t speed = 0;

  if (distance < FR_TAGGED_INT_MIN || distance > FR_TAGGED_INT_MAX) {
    return FR_ERR_RANGE;
  }
  if (distance == 0 && steps_to_stop <= 1) {
    motor->cells[FR_STEPPER_STEP_INTERVAL] = 0;
    motor->cells[FR_STEPPER_SPEED] = 0;
    motor->cells[FR_STEPPER_N] = 0;
    return FR_OK;
  }

  if (distance > 0) {
    if (n > 0 &&
        (steps_to_stop >= distance || motor->cells[FR_STEPPER_DIRECTION] < 0)) {
      n = -steps_to_stop;
    } else if (n < 0 && steps_to_stop < distance &&
               motor->cells[FR_STEPPER_DIRECTION] > 0) {
      n = -n;
    }
  } else if (distance < 0) {
    if (n > 0 && (steps_to_stop >= -distance ||
                  motor->cells[FR_STEPPER_DIRECTION] > 0)) {
      n = -steps_to_stop;
    } else if (n < 0 && steps_to_stop < -distance &&
               motor->cells[FR_STEPPER_DIRECTION] < 0) {
      n = -n;
    }
  }

  if (n == 0) {
    cn = motor->cells[FR_STEPPER_C0];
    motor->cells[FR_STEPPER_DIRECTION] = distance > 0 ? 1 : -1;
  } else {
    denominator = 4 * n + 1;
    correction = fr_stepper_divide_nearest(2 * cn, denominator);
    cn -= correction;
    if (cn < motor->cells[FR_STEPPER_CMIN]) {
      cn = motor->cells[FR_STEPPER_CMIN];
    }
  }

  n += 1;
  FR_TRY(fr_stepper_i32(n, &motor->cells[FR_STEPPER_N]));
  FR_TRY(fr_stepper_i32(cn, &motor->cells[FR_STEPPER_CN]));
  interval = (cn + 999) / 1000;
  FR_TRY(fr_stepper_i32(interval, &motor->cells[FR_STEPPER_STEP_INTERVAL]));
  speed = INT64_C(1000000000) / cn;
  if (motor->cells[FR_STEPPER_DIRECTION] < 0) {
    speed = -speed;
  }
  return fr_stepper_i32(speed, &motor->cells[FR_STEPPER_SPEED]);
}

static fr_err_t fr_stepper_store_speed(fr_runtime_t *runtime,
                                       const fr_stepper_motor_t *motor) {
  FR_TRY(fr_stepper_write(runtime, motor, FR_STEPPER_SPEED,
                          motor->cells[FR_STEPPER_SPEED]));
  FR_TRY(fr_stepper_write(runtime, motor, FR_STEPPER_STEP_INTERVAL,
                          motor->cells[FR_STEPPER_STEP_INTERVAL]));
  FR_TRY(fr_stepper_write(runtime, motor, FR_STEPPER_N,
                          motor->cells[FR_STEPPER_N]));
  FR_TRY(fr_stepper_write(runtime, motor, FR_STEPPER_CN,
                          motor->cells[FR_STEPPER_CN]));
  return fr_stepper_write(runtime, motor, FR_STEPPER_DIRECTION,
                          motor->cells[FR_STEPPER_DIRECTION]);
}

static fr_err_t fr_stepper_now(fr_int_t *out) {
  uint32_t micros = 0;

  FR_TRY(fr_platform_micros(&micros));
  return fr_stepper_i32(micros % ((uint32_t)FR_TAGGED_INT_MAX + 1u), out);
}

static uint32_t fr_stepper_elapsed(fr_int_t now, fr_int_t previous) {
  const uint32_t period = (uint32_t)FR_TAGGED_INT_MAX + 1u;

  if (now >= previous) {
    return (uint32_t)(now - previous);
  }
  return period - (uint32_t)previous + (uint32_t)now;
}

static fr_err_t fr_stepper_pulse(const fr_stepper_motor_t *motor) {
  fr_err_t delay_err = FR_OK;
  fr_err_t low_err = FR_OK;
  uint16_t step_pin = (uint16_t)motor->cells[FR_STEPPER_STEP_PIN];
  uint16_t direction_pin = (uint16_t)motor->cells[FR_STEPPER_DIRECTION_PIN];

  FR_TRY(fr_platform_gpio_write(
      direction_pin, motor->cells[FR_STEPPER_DIRECTION] > 0 ? 1 : 0));
  FR_TRY(fr_platform_delay_us(FR_STEPPER_MIN_PULSE_US));
  FR_TRY(fr_platform_gpio_write(step_pin, 1));
  delay_err = fr_platform_delay_us(FR_STEPPER_MIN_PULSE_US);
  low_err = fr_platform_gpio_write(step_pin, 0);
  return delay_err != FR_OK ? delay_err : low_err;
}

static fr_err_t fr_stepper_maybe_step(fr_runtime_t *runtime,
                                      fr_stepper_motor_t *motor,
                                      bool *out_stepped) {
  fr_int_t now = 0;
  fr_int_t position = 0;

  if (out_stepped == NULL) {
    return FR_ERR_INVALID;
  }
  *out_stepped = false;
  if (motor->cells[FR_STEPPER_STEP_INTERVAL] == 0) {
    return FR_OK;
  }

  FR_TRY(fr_stepper_now(&now));
  if (fr_stepper_elapsed(now, motor->cells[FR_STEPPER_LAST_STEP_TIME]) <
      (uint32_t)motor->cells[FR_STEPPER_STEP_INTERVAL]) {
    return FR_OK;
  }
  FR_TRY(fr_stepper_i32((int64_t)motor->cells[FR_STEPPER_CURRENT_POSITION] +
                            motor->cells[FR_STEPPER_DIRECTION],
                        &position));
  FR_TRY(fr_stepper_pulse(motor));

  motor->cells[FR_STEPPER_CURRENT_POSITION] = position;
  motor->cells[FR_STEPPER_LAST_STEP_TIME] = now;
  FR_TRY(
      fr_stepper_write(runtime, motor, FR_STEPPER_CURRENT_POSITION, position));
  FR_TRY(fr_stepper_write(runtime, motor, FR_STEPPER_LAST_STEP_TIME, now));
  *out_stepped = true;
  return FR_OK;
}

static fr_err_t fr_stepper_set_target(fr_runtime_t *runtime,
                                      fr_stepper_motor_t *motor,
                                      fr_int_t target) {
  int64_t distance =
      (int64_t)target - motor->cells[FR_STEPPER_CURRENT_POSITION];

  if (distance < FR_TAGGED_INT_MIN || distance > FR_TAGGED_INT_MAX) {
    return FR_ERR_RANGE;
  }
  if (target == motor->cells[FR_STEPPER_TARGET_POSITION]) {
    return FR_OK;
  }

  motor->cells[FR_STEPPER_TARGET_POSITION] = target;
  FR_TRY(fr_stepper_compute_speed(motor));
  FR_TRY(fr_stepper_write(runtime, motor, FR_STEPPER_TARGET_POSITION, target));
  return fr_stepper_store_speed(runtime, motor);
}

fr_err_t fr_lib_stepper_init(fr_runtime_t *runtime, const fr_tagged_t *args,
                             uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_tagged_t tagged[FR_STEPPER_CELL_COUNT] = {0};
  fr_int_t step_pin = 0;
  fr_int_t direction_pin = 0;
  fr_int_t now = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 3, out));
  FR_TRY(fr_stepper_object(runtime, args[0], &motor.object_id));
  FR_TRY(fr_stepper_decode_int(args, 1, &step_pin));
  FR_TRY(fr_stepper_decode_int(args, 2, &direction_pin));
  if (step_pin < 0 || step_pin > UINT16_MAX || direction_pin < 0 ||
      direction_pin > UINT16_MAX || step_pin == direction_pin) {
    return FR_ERR_DOMAIN;
  }

  motor.cells[FR_STEPPER_CURRENT_POSITION] = 0;
  motor.cells[FR_STEPPER_TARGET_POSITION] = 0;
  motor.cells[FR_STEPPER_SPEED] = 0;
  motor.cells[FR_STEPPER_MAX_SPEED] = 1;
  motor.cells[FR_STEPPER_ACCELERATION] = 1;
  motor.cells[FR_STEPPER_STEP_INTERVAL] = 0;
  FR_TRY(fr_stepper_now(&now));
  motor.cells[FR_STEPPER_LAST_STEP_TIME] = now;
  motor.cells[FR_STEPPER_N] = 0;
  FR_TRY(fr_stepper_c0(1, &motor.cells[FR_STEPPER_C0]));
  motor.cells[FR_STEPPER_CN] = motor.cells[FR_STEPPER_C0];
  FR_TRY(fr_stepper_cmin(1, &motor.cells[FR_STEPPER_CMIN]));
  motor.cells[FR_STEPPER_DIRECTION] = -1;
  motor.cells[FR_STEPPER_STEP_PIN] = step_pin;
  motor.cells[FR_STEPPER_DIRECTION_PIN] = direction_pin;
  motor.cells[FR_STEPPER_RESERVED] = 0;
  motor.cells[FR_STEPPER_SENTINEL] = FR_STEPPER_READY;

  for (uint16_t i = 0; i < FR_STEPPER_CELL_COUNT; i++) {
    FR_TRY(fr_tagged_encode_int(motor.cells[i], &tagged[i]));
  }
  FR_TRY(fr_stepper_write(runtime, &motor, FR_STEPPER_SENTINEL, 0));
  FR_TRY(fr_platform_gpio_mode((uint16_t)step_pin, 1));
  FR_TRY(fr_platform_gpio_mode((uint16_t)direction_pin, 1));
  FR_TRY(fr_platform_gpio_write((uint16_t)step_pin, 0));
  FR_TRY(fr_platform_gpio_write((uint16_t)direction_pin, 0));
  for (uint16_t i = 0; i < FR_STEPPER_SENTINEL; i++) {
    FR_TRY(fr_cells_write(runtime, motor.object_id, i, tagged[i]));
  }
  FR_TRY(fr_cells_write(runtime, motor.object_id, FR_STEPPER_SENTINEL,
                        tagged[FR_STEPPER_SENTINEL]));
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_stepper_set_max_speed(fr_runtime_t *runtime,
                                      const fr_tagged_t *args,
                                      uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_int_t requested = 0;
  int64_t speed = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 2, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_decode_int(args, 1, &requested));
  speed = requested < 0 ? -(int64_t)requested : requested;
  if (speed < 1 || speed > FR_STEPPER_MAX_CONFIGURED_SPEED) {
    return FR_ERR_DOMAIN;
  }
  if (speed == motor.cells[FR_STEPPER_MAX_SPEED]) {
    *out = fr_tagged_nil();
    return FR_OK;
  }

  motor.cells[FR_STEPPER_MAX_SPEED] = (fr_int_t)speed;
  FR_TRY(fr_stepper_cmin((fr_int_t)speed, &motor.cells[FR_STEPPER_CMIN]));
  if (motor.cells[FR_STEPPER_N] > 0) {
    FR_TRY(fr_stepper_i32(fr_stepper_steps_to_stop(&motor),
                          &motor.cells[FR_STEPPER_N]));
    FR_TRY(fr_stepper_compute_speed(&motor));
  }
  FR_TRY(fr_stepper_write(runtime, &motor, FR_STEPPER_MAX_SPEED,
                          motor.cells[FR_STEPPER_MAX_SPEED]));
  FR_TRY(fr_stepper_write(runtime, &motor, FR_STEPPER_CMIN,
                          motor.cells[FR_STEPPER_CMIN]));
  FR_TRY(fr_stepper_store_speed(runtime, &motor));
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_stepper_set_acceleration(fr_runtime_t *runtime,
                                         const fr_tagged_t *args,
                                         uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_int_t acceleration = 0;
  int64_t n = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 2, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_decode_int(args, 1, &acceleration));
  if (acceleration < 1 ||
      acceleration > FR_STEPPER_MAX_CONFIGURED_ACCELERATION) {
    return FR_ERR_DOMAIN;
  }
  if (acceleration == motor.cells[FR_STEPPER_ACCELERATION]) {
    *out = fr_tagged_nil();
    return FR_OK;
  }

  n = (int64_t)motor.cells[FR_STEPPER_N] *
      motor.cells[FR_STEPPER_ACCELERATION] / acceleration;
  FR_TRY(fr_stepper_i32(n, &motor.cells[FR_STEPPER_N]));
  FR_TRY(fr_stepper_c0(acceleration, &motor.cells[FR_STEPPER_C0]));
  motor.cells[FR_STEPPER_ACCELERATION] = acceleration;
  FR_TRY(fr_stepper_compute_speed(&motor));
  FR_TRY(
      fr_stepper_write(runtime, &motor, FR_STEPPER_ACCELERATION, acceleration));
  FR_TRY(fr_stepper_write(runtime, &motor, FR_STEPPER_C0,
                          motor.cells[FR_STEPPER_C0]));
  FR_TRY(fr_stepper_store_speed(runtime, &motor));
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_stepper_set_speed(fr_runtime_t *runtime,
                                  const fr_tagged_t *args, uint8_t arg_count,
                                  fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_int_t speed = 0;
  int64_t magnitude = 0;
  int64_t interval = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 2, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_decode_int(args, 1, &speed));
  if (speed > motor.cells[FR_STEPPER_MAX_SPEED]) {
    speed = motor.cells[FR_STEPPER_MAX_SPEED];
  } else if (speed < -motor.cells[FR_STEPPER_MAX_SPEED]) {
    speed = -motor.cells[FR_STEPPER_MAX_SPEED];
  }
  if (speed == motor.cells[FR_STEPPER_SPEED]) {
    *out = fr_tagged_nil();
    return FR_OK;
  }

  if (speed == 0) {
    motor.cells[FR_STEPPER_STEP_INTERVAL] = 0;
  } else {
    magnitude = speed < 0 ? -(int64_t)speed : speed;
    interval = (INT64_C(1000000) + magnitude - 1) / magnitude;
    FR_TRY(fr_stepper_i32(interval, &motor.cells[FR_STEPPER_STEP_INTERVAL]));
    motor.cells[FR_STEPPER_DIRECTION] = speed > 0 ? 1 : -1;
  }
  motor.cells[FR_STEPPER_SPEED] = speed;
  FR_TRY(fr_stepper_write(runtime, &motor, FR_STEPPER_SPEED, speed));
  FR_TRY(fr_stepper_write(runtime, &motor, FR_STEPPER_STEP_INTERVAL,
                          motor.cells[FR_STEPPER_STEP_INTERVAL]));
  FR_TRY(fr_stepper_write(runtime, &motor, FR_STEPPER_DIRECTION,
                          motor.cells[FR_STEPPER_DIRECTION]));
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_stepper_move_to(fr_runtime_t *runtime, const fr_tagged_t *args,
                                uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_int_t target = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 2, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_decode_int(args, 1, &target));
  FR_TRY(fr_stepper_set_target(runtime, &motor, target));
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_stepper_move(fr_runtime_t *runtime, const fr_tagged_t *args,
                             uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_int_t relative = 0;
  fr_int_t target = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 2, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_decode_int(args, 1, &relative));
  FR_TRY(fr_stepper_i32(
      (int64_t)motor.cells[FR_STEPPER_CURRENT_POSITION] + relative, &target));
  FR_TRY(fr_stepper_set_target(runtime, &motor, target));
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_stepper_run_speed(fr_runtime_t *runtime,
                                  const fr_tagged_t *args, uint8_t arg_count,
                                  fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  bool stepped = false;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 1, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_maybe_step(runtime, &motor, &stepped));
  return fr_tagged_encode_bool(stepped, out);
}

fr_err_t fr_lib_stepper_run(fr_runtime_t *runtime, const fr_tagged_t *args,
                            uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  bool stepped = false;
  bool running = false;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 1, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_maybe_step(runtime, &motor, &stepped));
  if (stepped) {
    FR_TRY(fr_stepper_compute_speed(&motor));
    FR_TRY(fr_stepper_store_speed(runtime, &motor));
  }
  running =
      motor.cells[FR_STEPPER_SPEED] != 0 || fr_stepper_distance(&motor) != 0;
  return fr_tagged_encode_bool(running, out);
}

fr_err_t fr_lib_stepper_stop(fr_runtime_t *runtime, const fr_tagged_t *args,
                             uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  int64_t distance = 0;
  fr_int_t target = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 1, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  if (motor.cells[FR_STEPPER_SPEED] != 0) {
    distance = fr_stepper_steps_to_stop(&motor) + 1;
    if (motor.cells[FR_STEPPER_SPEED] < 0) {
      distance = -distance;
    }
    FR_TRY(fr_stepper_i32(
        (int64_t)motor.cells[FR_STEPPER_CURRENT_POSITION] + distance, &target));
    FR_TRY(fr_stepper_set_target(runtime, &motor, target));
  }
  *out = fr_tagged_nil();
  return FR_OK;
}

fr_err_t fr_lib_stepper_set_current_position(fr_runtime_t *runtime,
                                             const fr_tagged_t *args,
                                             uint8_t arg_count,
                                             fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_int_t position = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 2, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_decode_int(args, 1, &position));
  motor.cells[FR_STEPPER_CURRENT_POSITION] = position;
  motor.cells[FR_STEPPER_TARGET_POSITION] = position;
  motor.cells[FR_STEPPER_SPEED] = 0;
  motor.cells[FR_STEPPER_STEP_INTERVAL] = 0;
  motor.cells[FR_STEPPER_N] = 0;
  FR_TRY(
      fr_stepper_write(runtime, &motor, FR_STEPPER_CURRENT_POSITION, position));
  FR_TRY(
      fr_stepper_write(runtime, &motor, FR_STEPPER_TARGET_POSITION, position));
  FR_TRY(fr_stepper_store_speed(runtime, &motor));
  *out = fr_tagged_nil();
  return FR_OK;
}

static fr_err_t fr_stepper_query(fr_runtime_t *runtime, const fr_tagged_t *args,
                                 uint8_t arg_count, fr_tagged_t *out,
                                 uint16_t index) {
  fr_stepper_motor_t motor = {0};

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 1, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  return fr_tagged_encode_int(motor.cells[index], out);
}

fr_err_t fr_lib_stepper_current_position(fr_runtime_t *runtime,
                                         const fr_tagged_t *args,
                                         uint8_t arg_count, fr_tagged_t *out) {
  return fr_stepper_query(runtime, args, arg_count, out,
                          FR_STEPPER_CURRENT_POSITION);
}

fr_err_t fr_lib_stepper_target_position(fr_runtime_t *runtime,
                                        const fr_tagged_t *args,
                                        uint8_t arg_count, fr_tagged_t *out) {
  return fr_stepper_query(runtime, args, arg_count, out,
                          FR_STEPPER_TARGET_POSITION);
}

fr_err_t fr_lib_stepper_distance_to_go(fr_runtime_t *runtime,
                                       const fr_tagged_t *args,
                                       uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  fr_int_t distance = 0;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 1, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  FR_TRY(fr_stepper_i32(fr_stepper_distance(&motor), &distance));
  return fr_tagged_encode_int(distance, out);
}

fr_err_t fr_lib_stepper_speed(fr_runtime_t *runtime, const fr_tagged_t *args,
                              uint8_t arg_count, fr_tagged_t *out) {
  return fr_stepper_query(runtime, args, arg_count, out, FR_STEPPER_SPEED);
}

fr_err_t fr_lib_stepper_running(fr_runtime_t *runtime, const fr_tagged_t *args,
                                uint8_t arg_count, fr_tagged_t *out) {
  fr_stepper_motor_t motor = {0};
  bool running = false;

  FR_TRY(fr_stepper_check_call(runtime, args, arg_count, 1, out));
  FR_TRY(fr_stepper_load(runtime, args[0], &motor));
  running =
      motor.cells[FR_STEPPER_SPEED] != 0 || fr_stepper_distance(&motor) != 0;
  return fr_tagged_encode_bool(running, out);
}
