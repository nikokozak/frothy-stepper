#!/usr/bin/env bash
set -euo pipefail

: "${FROTHY_SOURCE_ROOT:?set FROTHY_SOURCE_ROOT to a Frothy source checkout}"

root=$(cd "$(dirname "$0")/.." && pwd)
frothy=${FROTHY:-$FROTHY_SOURCE_ROOT/build/host/frothy}
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/frothy-stepper.XXXXXX")
ln -s "$root" "$build_dir/frothy-stepper"
project="$build_dir/frothy-stepper/test"

cleanup() {
  rm -rf "$project/.frothy" "$build_dir"
}
trap cleanup EXIT

export FROTHY_SOURCE_ROOT
make -C "$FROTHY_SOURCE_ROOT" frothy-host-command >/dev/null
PROFILE=host_normal BUILD_DIR="$build_dir" \
  "$frothy" build --project "$project" >/dev/null
binary="$build_dir/frothy.elf"
generated="$project/.frothy/build/host"

grep -Fq 'native/stepper.c' "$generated/libs.cmake"
grep -Fq '"stepper.running?", fr_lib_stepper_running, 1' \
  "$generated/lib_natives.c"

numbers() {
  awk '/^> -?[0-9]+$/ { sub(/^> /, ""); print }'
}

# Integer projection of AccelStepper 1.66's float recurrence for a ten-step
# move at acceleration 100. Each row is position, signed speed, interval us.
trace_output=$(
  "$binary" <<'EOF'
m is cells(16)
stepper.init: m, 2, 3
stepper.set-max-speed: m, 1000
stepper.set-acceleration: m, 100
stepper.move-to: m, 10
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
ms: 100
stepper.run: m
stepper.current-position: m
stepper.speed: m
m[5]
stepper.run: m
stepper.current-position: m
stepper.speed: m
gpio.read: 2
EOF
)
trace=$(printf '%s\n' "$trace_output" | numbers)
expected_trace='0
10
95601
1
17
57361
2
22
44614
3
26
37751
4
30
33309
5
33
30137
6
30
33309
7
26
37751
8
22
44614
9
17
57361
10
0
0
10
0
0'
if [ "$trace" != "$expected_trace" ]; then
  printf 'acceleration trace mismatch\nexpected:\n%s\nactual:\n%s\n' \
    "$expected_trace" "$trace"
  exit 1
fi
if ! printf '%s\n' "$trace_output" | grep -q '^> false$'; then
  printf 'completed move did not stay stopped\n'
  exit 1
fi

# Reverse the target while accelerating, then prove exact landing and a low
# idle step pin. This also exercises a generated native name ending in `?`.
reverse_output=$(
  "$binary" <<'EOF'
m is cells(16)
stepper.init: m, 2, 3
stepper.set-max-speed: m, 1000
stepper.set-acceleration: m, 100
stepper.move-to: m, 100
repeat 4 [ ms: 100 ; stepper.run: m ]
stepper.current-position: m
stepper.speed: m
stepper.move-to: m, -3
repeat 40 [ ms: 100 ; stepper.run: m ]
stepper.current-position: m
stepper.target-position: m
stepper.distance-to-go: m
stepper.speed: m
stepper.running?: m
gpio.read: 2
EOF
)
reverse=$(printf '%s\n' "$reverse_output" | numbers)
expected_reverse='4
30
-3
-3
0
0
0'
if [ "$reverse" != "$expected_reverse" ] ||
   ! printf '%s\n' "$reverse_output" | grep -q '^> false$'; then
  printf 'mid-motion reversal mismatch\n%s\n' "$reverse_output"
  exit 1
fi

# Pin the accepted numeric envelope and complete a move without a range error.
boundary_output=$(
  "$binary" <<'EOF'
m is cells(16)
stepper.init: m, 8, 9
stepper.set-max-speed: m, 46340
stepper.set-acceleration: m, 1
stepper.move-to: m, 5
repeat 10 [ ms: 1000 ; stepper.run: m ]
stepper.current-position: m
stepper.target-position: m
stepper.distance-to-go: m
stepper.speed: m
stepper.running?: m
EOF
)
boundary=$(printf '%s\n' "$boundary_output" | numbers)
expected_boundary='5
5
0
0'
if [ "$boundary" != "$expected_boundary" ] ||
   ! printf '%s\n' "$boundary_output" | grep -q '^> false$' ||
   printf '%s\n' "$boundary_output" | grep -q '^> error:'; then
  printf 'numeric boundary mismatch\n%s\n' "$boundary_output"
  exit 1
fi

# Cross the 2^30 visible-micros wrap. Pulse delays advance the host clock by
# four microseconds, so the second due step lands at wrapped time 184.
wrap_output=$(
  "$binary" <<'EOF'
m is cells(16)
stepper.init: m, 2, 3
stepper.set-max-speed: m, 1000
stepper.set-speed: m, 1000
repeat 16 [ ms: 65535 ]
ms: 25181
stepper.run-speed: m
micros:
ms: 1
stepper.run-speed: m
micros:
stepper.current-position: m
EOF
)
wrap=$(printf '%s\n' "$wrap_output" | numbers)
expected_wrap='1073741004
184
2'
if [ "$wrap" != "$expected_wrap" ]; then
  printf 'micros wrap mismatch\n%s\n' "$wrap_output"
  exit 1
fi

uninitialized_output=$(
  "$binary" <<'EOF'
raw is cells(16)
stepper.run-speed: raw
EOF
)
if ! printf '%s\n' "$uninitialized_output" | grep -q '^> error:'; then
  printf 'uninitialized motor did not fail\n%s\n' "$uninitialized_output"
  exit 1
fi

printf 'frothy-stepper tests ok\n'
