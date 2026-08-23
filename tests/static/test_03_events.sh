#!/bin/bash
# ---------------------------------------------------------------------------
# test_events.sh — Event (interrupt threshold) configuration tests
#
# Covers: PS threshold rising/falling values, PS enable/period,
# LS threshold rising/falling values, LS enable/period,
# LS variance value/enable, mutual exclusion between threshold and
# variance modes, and event attribute available files.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib.sh"

find_device
trap reset_sensor EXIT
reset_sensor

EV="events"

begin_suite "Events"

# === PS threshold values (11-bit: 0–2047) ===

test_write_read "PS thresh rising: write 500"    "$EV/in_proximity_thresh_rising_value"   500
test_write_read "PS thresh rising: write 0"      "$EV/in_proximity_thresh_rising_value"   0
test_write_read "PS thresh rising: write 2047"   "$EV/in_proximity_thresh_rising_value"   2047
test_write_read "PS thresh falling: write 100"   "$EV/in_proximity_thresh_falling_value"  100
test_write_read "PS thresh falling: write 0"     "$EV/in_proximity_thresh_falling_value"  0

# === PS enable and period ===

test_write_read "PS thresh enable: write 1"      "$EV/in_proximity_thresh_en"      1
test_write_read "PS thresh enable: write 0"      "$EV/in_proximity_thresh_en"      0
test_write_read "PS thresh period: write 5"      "$EV/in_proximity_thresh_period"  5
test_write_read "PS thresh period: write 1"      "$EV/in_proximity_thresh_period"  1
test_write_read "PS thresh period: write 16"     "$EV/in_proximity_thresh_period"  16

# === LS threshold values (20-bit: 0–1048575) ===

test_write_read "LS thresh rising: write 10000"   "$EV/in_illuminance_thresh_rising_value"   10000
test_write_read "LS thresh rising: write 0"       "$EV/in_illuminance_thresh_rising_value"   0
test_write_read "LS thresh rising: write 1048575" "$EV/in_illuminance_thresh_rising_value"   1048575
test_write_read "LS thresh falling: write 5000"   "$EV/in_illuminance_thresh_falling_value"  5000
test_write_read "LS thresh falling: write 0"      "$EV/in_illuminance_thresh_falling_value"  0

# === LS enable and period ===

test_write_read "LS thresh enable: write 1"      "$EV/in_illuminance_thresh_en"      1
test_write_read "LS thresh enable: write 0"      "$EV/in_illuminance_thresh_en"      0
test_write_read "LS thresh period: write 3"      "$EV/in_illuminance_thresh_period"  3
test_write_read "LS thresh period: write 1"      "$EV/in_illuminance_thresh_period"  1
test_write_read "LS thresh period: write 16"     "$EV/in_illuminance_thresh_period"  16

# === LS variance event (change type) ===
# Value must be a power of 2 in [8, 1024]

test_write_read "LS variance value: write 8"     "$EV/in_illuminance_change_value"  8
test_write_read "LS variance value: write 64"    "$EV/in_illuminance_change_value"  64
test_write_read "LS variance value: write 1024"  "$EV/in_illuminance_change_value"  1024
test_write_read "LS variance enable: write 1"    "$EV/in_illuminance_change_en"     1
test_write_read "LS variance enable: write 0"    "$EV/in_illuminance_change_en"     0

# === Mutual exclusion: enabling threshold disables variance and vice versa ===

# Enable variance first
write_attr "$EV/in_illuminance_change_en" 1
val="$(read_attr "$EV/in_illuminance_change_en")"
expect_eq "variance enabled" "1" "$val"

# Now enable threshold — should clear variance mode
write_attr "$EV/in_illuminance_thresh_en" 1
val="$(read_attr "$EV/in_illuminance_change_en")"
expect_eq "variance disabled after threshold enable" "0" "$val"

val="$(read_attr "$EV/in_illuminance_thresh_en")"
expect_eq "threshold still enabled" "1" "$val"

# Now enable variance — should clear threshold mode
write_attr "$EV/in_illuminance_change_en" 1
val="$(read_attr "$EV/in_illuminance_thresh_en")"
expect_eq "threshold disabled after variance enable" "0" "$val"

# Disable variance
write_attr "$EV/in_illuminance_change_en" 0

# === Event attribute files ===

val="$(read_attr "$EV/ls_int_sel")"
expect_in "ls_int_sel: contains a valid value" "green" "$val"

val="$(read_attr "$EV/ls_int_sel_available")"
expect_in "ls_int_sel_available: contains ir"    "ir"     "$val"
expect_in "ls_int_sel_available: contains green"  "green"  "$val"
expect_in "ls_int_sel_available: contains red"    "red"    "$val"
expect_in "ls_int_sel_available: contains blue"   "blue"   "$val"

test_write_read "ls_int_sel: write red"   "$EV/ls_int_sel"  "red"
test_write_read "ls_int_sel: write ir"    "$EV/ls_int_sel"  "ir"
test_write_read "ls_int_sel: write blue"  "$EV/ls_int_sel"  "blue"
test_write_read "ls_int_sel: write green" "$EV/ls_int_sel"  "green"

val="$(read_attr "$EV/ps_logic_mode")"
expect_in "ps_logic_mode: valid value" "latched" "$val"

val="$(read_attr "$EV/ps_logic_mode_available")"
expect_in "ps_logic_mode_available: contains latched"  "latched"  "$val"
expect_in "ps_logic_mode_available: contains pulsed"   "pulsed"   "$val"

test_write_read "ps_logic_mode: write pulsed"   "$EV/ps_logic_mode"  "pulsed"
test_write_read "ps_logic_mode: write latched"  "$EV/ps_logic_mode"  "latched"

end_suite
