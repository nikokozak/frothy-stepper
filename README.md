# frothy-stepper-gpl

An accelerated step-and-direction motor library for
[Frothy](https://frothy.dev), implemented as a Frothy C extension.

> **GPL warning:** this optional library is GPLv3, not MIT. If you distribute
> firmware containing it, expect GPLv3 source, license, and installation
> obligations to apply to the covered work. Private use does not itself
> require publication. This is practical project guidance, not legal advice.

Frothy itself remains MIT licensed. Projects that do not include this library
do not acquire obligations from it.

## What it supports

Version 0.1 drives the `DRIVER` interface used by A4988-, DRV8825-, and similar
step-and-direction controllers. It provides nonblocking constant-speed and
accelerated motion, absolute and relative moves, decelerating stop, position
queries, and multiple independent motors.

It does not include coil sequencing, `MultiStepper`, blocking move calls,
enable or inverted pins, callbacks, floating-point settings, or timer/ISR
stepping.

## Install

Pin the library to a commit in the project's `frothy.toml`:

```toml
[deps]
frothy-stepper-gpl = { git = "https://github.com/nikokozak/frothy-stepper-gpl", rev = "<commit>" }
```

The library requires a 32-bit Frothy profile with cells, GPIO, the C-extension
include path, and `fr_platform_delay_us`. The normal ESP32 profile qualifies;
tiny profiles intentionally do not.

## Use

Allocate the state at top level, initialize the pins, and service the motor
frequently. Each `stepper.run` call emits at most one step.

```frothy
motor is cells(16)

to boot [
  stepper.init: motor, 2, 3
  stepper.set-max-speed: motor, 1200
  stepper.set-acceleration: motor, 300
  stepper.move-to: motor, 6400

  forever [ stepper.run: motor ]
]
```

`stepper.init` starts with deliberately conservative defaults of 1 step/s and
1 step/s². Set both the maximum speed and acceleration before starting an
accelerated move, as in the example above.

For constant speed, call `stepper.set-speed` and service
`stepper.run-speed`. A negative speed reverses direction; zero stops pulses.

```frothy
stepper.set-speed: motor, -500
forever [ stepper.run-speed: motor ]
```

Service multiple motors from the same loop:

```frothy
forever [
  stepper.run: x-motor
  stepper.run: y-motor
]
```

## Words

| Word | Arguments | Result | Purpose |
|---|---|---|---|
| `stepper.init` | motor, step pin, direction pin | nil | Initialize one `cells(16)` and both outputs. |
| `stepper.set-max-speed` | motor, steps/s | nil | Set the positive speed ceiling. |
| `stepper.set-acceleration` | motor, steps/s² | nil | Set positive acceleration. |
| `stepper.set-speed` | motor, signed steps/s | nil | Set clamped constant speed. |
| `stepper.move-to` | motor, absolute position | nil | Set an accelerated destination. |
| `stepper.move` | motor, relative steps | nil | Set a destination relative to the current position. |
| `stepper.run-speed` | motor | bool | Emit one due constant-speed step; true if stepped. |
| `stepper.run` | motor | bool | Service accelerated motion; true while work remains. |
| `stepper.stop` | motor | nil | Change the target to decelerate normally. |
| `stepper.set-current-position` | motor, position | nil | Rebase current and target position and stop. |
| `stepper.current-position` | motor | int | Read the current step count. |
| `stepper.target-position` | motor | int | Read the target step count. |
| `stepper.distance-to-go` | motor | int | Read target minus current position. |
| `stepper.speed` | motor | int | Read signed current speed. |
| `stepper.running?` | motor | bool | Report motion or a remaining destination. |

A value other than `cells(16)`, invalid configuration, and detected malformed
or uninitialized state produce a Frothy error before a step is emitted. The
sentinel and range checks catch common damage; they are not an integrity check
for every possible cell mutation.

## State and limits

All per-motor state lives in the caller's cells. The extension retains no
object pointer, hidden allocation, handle, cache, or mutable C state.

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

Settings are integers. Maximum configured speed is 46,340 steps/s and
acceleration is 1–100,000 steps/s². The recurrence uses 64-bit intermediates
and stores deterministic fixed-point values back into tagged cells. Cell 14
preserves fractional speed for stopping-distance decisions; `stepper.speed`
returns its integer projection. Pulse intervals round upward to a whole
microsecond, a conservative difference from AccelStepper's floating-point
interval truncation. Fixed-point rounding, polling frequency, GPIO cost, and
the motor itself can all make the physically useful ceiling lower than the
accepted numeric ceiling.

Positions use Frothy's signed 30-bit integer range. Relative moves,
target-distance calculations, and free-running steps return a range error at
that boundary instead of wrapping.

Each pulse has 2 µs of direction setup and at least 2 µs high time. That timing
is enforced by Frothy's platform layer, but the first release is not yet
logic-analyzer-qualified on every supported board and driver. Do not use it
where a missed step could create a safety hazard. Timer-driven stepping is a
separate future capability.

## Wiring

- Frothy `step pin` → controller STEP.
- Frothy `direction pin` → controller DIR.
- Board ground → controller ground.
- Motor power → a suitable external supply; do not power a stepper motor from
  a GPIO pin.

Follow the controller's current-limit, decoupling, and power-sequencing rules.

## Test the extension path

The repository test builds a real Frothy project with the path dependency,
generates the native table, compiles this C source into the runtime, and checks
motion, reversal, numeric limits, wrapped microsecond timing, and invalid
state:

```sh
FROTHY_SOURCE_ROOT=/path/to/FrothyRewrite ./test/test.sh
```

## Origin and license

The motion algorithm is a modified translation of the `DRIVER` path in
[AccelStepper 1.66](https://www.airspayce.com/mikem/arduino/AccelStepper/),
Copyright 2009–2025 Mike McCauley. AccelStepper and Mike McCauley do not sponsor
or maintain this Frothy port.

The translation and this repository are licensed under GPLv3 only; see
`LICENSE`. A future permissive `frothy-stepper` must be independently designed,
not relicensed or translated back from this implementation.
