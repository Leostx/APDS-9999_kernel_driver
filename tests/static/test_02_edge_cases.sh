#!/bin/bash
# ---------------------------------------------------------------------------
# test_edge_cases.sh — Boundary values, invalid inputs, and mode exclusivity
#
# Covers: RGB/ALS mode exclusivity for channel reads, out-of-range writes
# for thresholds/calibbias/pulses/gain/period/variance, the 13-bit
# resolution edge case (no scale defined), and independence between
# digital and analog cancellation registers.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib.sh"

find_device
trap reset_sensor EXIT
reset_sensor

EV="events"

begin_suite "Edge Cases"

# =========================================================================
# Mode exclusivity: reading wrong channel type for current mode
# =========================================================================

# In RGB mode (default): ALS channel reads should fail with -EBUSY
write_attr rgb_mode 1
sleep 0.1

if read_attr in_illuminance_raw >/dev/null 2>&1; then
    fail "ALS raw in RGB mode: should fail"
else
    pass "ALS raw in RGB mode: correctly rejected"
fi

if read_attr in_illuminance_input >/dev/null 2>&1; then
    fail "ALS processed in RGB mode: should fail"
else
    pass "ALS processed in RGB mode: correctly rejected"
fi

# In ALS mode: RGB channel reads should fail with -EBUSY
write_attr rgb_mode 0
sleep 0.2

if read_attr in_intensity_red_raw >/dev/null 2>&1; then
    fail "RED raw in ALS mode: should fail"
else
    pass "RED raw in ALS mode: correctly rejected"
fi

if read_attr in_intensity_blue_input >/dev/null 2>&1; then
    fail "BLUE processed in ALS mode: should fail"
else
    pass "BLUE processed in ALS mode: correctly rejected"
fi

# IR is always readable regardless of mode
val="$(read_attr in_intensity_ir_raw)"
expect_numeric "IR raw always readable in ALS mode" "$val"

write_attr rgb_mode 1
sleep 0.2

val="$(read_attr in_intensity_ir_raw)"
expect_numeric "IR raw always readable in RGB mode" "$val"

# =========================================================================
# PS threshold boundary values (11-bit: 0–2047)
# =========================================================================

expect_write_fail "PS thresh rising > 2047"   "$EV/in_proximity_thresh_rising_value"   2048
expect_write_fail "PS thresh rising < 0"      "$EV/in_proximity_thresh_rising_value"   -1
expect_write_fail "PS thresh falling > 2047"  "$EV/in_proximity_thresh_falling_value"  2048
expect_write_fail "PS thresh falling < 0"     "$EV/in_proximity_thresh_falling_value"  -1

# =========================================================================
# LS threshold boundary values (20-bit: 0–1048575)
# =========================================================================

expect_write_fail "LS thresh rising > max"    "$EV/in_illuminance_thresh_rising_value"   1048576
expect_write_fail "LS thresh rising < 0"      "$EV/in_illuminance_thresh_rising_value"   -1
expect_write_fail "LS thresh falling > max"   "$EV/in_illuminance_thresh_falling_value"  1048576
expect_write_fail "LS thresh falling < 0"     "$EV/in_illuminance_thresh_falling_value"  -1

# =========================================================================
# Calibbias boundary values (11-bit: 0–2047)
# =========================================================================

expect_write_fail "calibbias > 2047"   in_proximity_calibbias  2048
expect_write_fail "calibbias < 0"      in_proximity_calibbias  -1

# =========================================================================
# Analog cancellation boundary values (5-bit: 0–31)
# =========================================================================

expect_write_fail "ana_can > 31"   ps_analog_cancellation  32
expect_write_fail "ana_can < 0"    ps_analog_cancellation  -1

# =========================================================================
# Invalid hardware gain values
# =========================================================================

expect_write_fail "gain = 0"    in_intensity_hardwaregain  0
expect_write_fail "gain = 5"    in_intensity_hardwaregain  5
expect_write_fail "gain = 20"   in_intensity_hardwaregain  20
expect_write_fail "gain = -1"   in_intensity_hardwaregain  -1

# =========================================================================
# Invalid period values (valid: 1–16)
# =========================================================================

expect_write_fail "PS period = 0"    "$EV/in_proximity_thresh_period"   0
expect_write_fail "PS period = 17"   "$EV/in_proximity_thresh_period"   17
expect_write_fail "LS period = 0"    "$EV/in_illuminance_thresh_period" 0
expect_write_fail "LS period = 17"   "$EV/in_illuminance_thresh_period" 17

# =========================================================================
# Invalid variance values (must be power of 2 in [8, 1024])
# =========================================================================

expect_write_fail "variance = 7 (not power of 2)"   "$EV/in_illuminance_change_value"  7
expect_write_fail "variance = 4 (below min 8)"      "$EV/in_illuminance_change_value"  4
expect_write_fail "variance = 2048 (above max)"     "$EV/in_illuminance_change_value"  2048
expect_write_fail "variance = 0"                    "$EV/in_illuminance_change_value"  0

# =========================================================================
# PS pulses boundary (0–255)
# =========================================================================

expect_write_fail "ps_pulses = 256"   ps_pulses  256
expect_write_fail "ps_pulses = -1"    ps_pulses  -1

# =========================================================================
# 13-bit resolution: scale and processed return -EINVAL
# =========================================================================

write_attr ls_reso_bit 13
sleep 0.1
write_attr rgb_mode 0
sleep 0.2

# Scale has no defined factor for 13-bit
if read_attr in_illuminance_scale >/dev/null 2>&1; then
    fail "scale at 13-bit: should fail"
else
    pass "scale at 13-bit: correctly rejected"
fi

# Processed value also fails at 13-bit (no scale → can't compute)
if read_attr in_illuminance_input >/dev/null 2>&1; then
    fail "processed at 13-bit: should fail"
else
    pass "processed at 13-bit: correctly rejected"
fi

# Raw read should still work at 13-bit
val="$(read_attr in_illuminance_raw)"
expect_numeric "raw at 13-bit: still readable" "$val"

write_attr rgb_mode 1
sleep 0.2

# =========================================================================
# Calibbias / analog cancellation independence (shared register pair)
#
# PS_CAN_0 = low 8 bits of digital cancellation
# PS_CAN_1 = [2:0] high 3 bits of digital, [7:3] analog cancellation
# Writing one must not clobber the other.
# =========================================================================

# Set digital=500, then set analog=20, verify digital unchanged
write_attr in_proximity_calibbias 500
write_attr ps_analog_cancellation 20

val="$(read_attr in_proximity_calibbias)"
expect_eq "calibbias preserved after ana_can write" "500" "$val"

val="$(read_attr ps_analog_cancellation)"
expect_eq "ana_can reads 20" "20" "$val"

# Change digital to 1000, verify analog unchanged
write_attr in_proximity_calibbias 1000

val="$(read_attr ps_analog_cancellation)"
expect_eq "ana_can preserved after calibbias write" "20" "$val"

val="$(read_attr in_proximity_calibbias)"
expect_eq "calibbias reads 1000" "1000" "$val"

# =========================================================================
# ps_overflow is read-only
# =========================================================================

expect_write_fail "write to ps_overflow"  ps_overflow  "1"
expect_write_fail "write to ps_overflow"  ps_overflow  "0"

# =========================================================================
# Write to read-only _available files should fail
# =========================================================================

expect_write_fail "write to ps_reso_bit_available"     ps_reso_bit_available     "11"
expect_write_fail "write to ps_meas_rate_us_available"  ps_meas_rate_us_available "50000"

end_suite
