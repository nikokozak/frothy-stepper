# Frothy Stepper

Frothy Stepper adds smooth, accelerated motion to
[Frothy](https://frothy.dev) projects that use a step-and-direction motor
driver such as an A4988 or DRV8825.

**Current version:** 0.1.0

**Status:** Experimental

The library passes its host-side motion and integration tests, but it has not
yet been qualified with physical motors and a logic analyzer. Expect the API,
timing limits, and internal state layout to change as real hardware testing
begins. Do not use this release where a missed step could cause damage or
injury.

## What you need

- A Frothy-supported ESP32 board.
- A step-and-direction motor driver.
- A suitable external motor power supply.
- A 32-bit Frothy profile with cells and GPIO support.

The library currently declares support for `esp32_devkit_v1` and
`seeed_xiao_esp32s3`. The `host` target is included for testing.

This is a Frothy library with a C component. Its native words must be compiled
into the firmware, so adding the dependency requires rebuilding and flashing
Frothy. Sending source to an existing standard Frothy firmware is not enough.

## Add it to a project

Add the library to your project's `frothy.toml`, pinned to a commit:

```toml
[deps]
frothy-stepper = { git = "https://github.com/nikokozak/frothy-stepper", rev = "<commit-sha>" }
```

Build the project, then flash the resulting firmware. Replace the board and
port with the ones you are using:

```sh
frothy build
frothy flash esp32_devkit_v1 --port /dev/cu.usbserial-0001
```

Version 0.1.0 contains only native words, so there is no additional Frothy
library source to install after flashing.

## Wire the driver

- Connect a Frothy GPIO pin to the driver's STEP input.
- Connect another GPIO pin to the driver's DIR input.
- Connect the board ground to the driver ground.
- Power the motor from a suitable external supply.

Do not power a stepper motor from a GPIO pin. Follow the driver's instructions
for current limiting, decoupling, and power sequencing.

## Move a motor

Each motor uses one caller-owned `cells(16)`. Initialize it with the STEP and
DIR pins, choose a maximum speed and acceleration, and set a destination:

```frothy
motor is cells(16)

stepper.init: motor, 2, 3
stepper.set-max-speed: motor, 1200
stepper.set-acceleration: motor, 300
stepper.move-to: motor, 6400

forever [ stepper.run: motor ]
```

`stepper.run` is nonblocking and emits at most one step each time it is called.
Call it frequently while the motor is moving. The example moves to absolute
position 6400; the unit is one driver step.

`stepper.init` begins with deliberately slow defaults of 1 step/s and
1 step/s². Set the maximum speed and acceleration before starting normal
accelerated motion.

For a relative move, use `stepper.move`:

```frothy
stepper.move: motor, -800
```

For constant-speed motion, set a signed speed and call `stepper.run-speed`:

```frothy
stepper.set-speed: motor, -500
forever [ stepper.run-speed: motor ]
```

A negative speed reverses direction. A speed of zero stops new pulses.

Multiple motors can share one service loop:

```frothy
forever [
  stepper.run: x-motor
  stepper.run: y-motor
]
```

## Words

| Word | Arguments | Result | What it does |
|---|---|---|---|
| `stepper.init` | motor, step pin, direction pin | nil | Initialize one `cells(16)` and both output pins. |
| `stepper.set-max-speed` | motor, steps/s | nil | Set the positive speed limit. |
| `stepper.set-acceleration` | motor, steps/s² | nil | Set positive acceleration. |
| `stepper.set-speed` | motor, signed steps/s | nil | Set a constant speed, clamped to the speed limit. |
| `stepper.move-to` | motor, position | nil | Set an absolute destination. |
| `stepper.move` | motor, steps | nil | Set a destination relative to the current position. |
| `stepper.run-speed` | motor | bool | Emit one constant-speed step if it is due. |
| `stepper.run` | motor | bool | Service accelerated motion; true while work remains. |
| `stepper.stop` | motor | nil | Set a new target that brings the motor to a normal decelerating stop. |
| `stepper.set-current-position` | motor, position | nil | Change the current and target position and stop. |
| `stepper.current-position` | motor | int | Read the current position. |
| `stepper.target-position` | motor | int | Read the destination. |
| `stepper.distance-to-go` | motor | int | Read destination minus current position. |
| `stepper.speed` | motor | int | Read the signed current speed. |
| `stepper.running?` | motor | bool | Report whether motion remains. |

Passing anything other than `cells(16)`, using invalid settings, or passing
uninitialized or visibly damaged state produces a Frothy error before a step
is emitted.

## Current limits

- Settings are integers; the library does not use floating-point values.
- Maximum accepted speed is 46,340 steps/s.
- Accepted acceleration is 1–100,000 steps/s².
- Positions use Frothy's signed 30-bit integer range.
- Each pulse includes 2 µs of direction setup and at least 2 µs high time.
- Stepping is polling-based, not timer- or interrupt-driven.
- Coil sequencing, `MultiStepper`, blocking moves, enable pins, inverted pins,
  callbacks, and configurable pulse width are not included.

The useful physical speed may be much lower than the numeric limit. It depends
on how often the service word runs, GPIO overhead, the driver, the motor, its
load, supply voltage, and wiring.

## Inspecting motor state

The extension keeps no hidden motor allocation or mutable C state. Everything
for a motor lives in its `cells(16)`, so experienced users can inspect it
directly. Normal programs should use the public words above instead of writing
these cells.

| Cell | Value | Cell | Value |
|---:|---|---:|---|
| 0 | current position | 8 | initial interval × 1000 |
| 1 | target position | 9 | current interval × 1000 |
| 2 | signed speed | 10 | minimum interval × 1000 |
| 3 | maximum speed | 11 | direction, -1 or 1 |
| 4 | acceleration | 12 | step pin |
| 5 | step interval in µs | 13 | direction pin |
| 6 | wrapped last-step time | 14 | signed speed × 1000 |
| 7 | acceleration recurrence step | 15 | initialization sentinel |

## Test it

The test builds a real Frothy project, compiles the extension into the host
runtime, and checks acceleration, reversal, stopping, numeric limits, wrapped
microsecond timing, invalid state, and generated native wiring:

```sh
FROTHY_SOURCE_ROOT=/path/to/FrothyRewrite ./test/test.sh
```

Passing this test does not replace physical motor and timing tests.

## Origin and license

This is a modified translation of the `DRIVER` motion algorithm in
[AccelStepper 1.66](https://www.airspayce.com/mikem/arduino/AccelStepper/) by
Mike McCauley. The project is licensed under GPLv3; see `LICENSE`. AccelStepper
and Mike McCauley do not sponsor or maintain this Frothy port.
