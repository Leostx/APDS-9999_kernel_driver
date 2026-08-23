#!/bin/bash
# ---------------------------------------------------------------------------
# test_direct_mode.sh — Direct mode (sysfs read/write) tests
#
# Covers: boolean controls, raw channel reads, processed values, hardware
# gain, scale, calibbias, all custom sysfs attributes, and _available files.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib.sh"

find_device
trap reset_sensor EXIT
reset_sensor

begin_suite "Direct Mode"

# === Boolean attributes ===

test_write_read "ps_enable: write 0"   ps_enable  0
test_write_read "ps_enable: write 1"   ps_enable  1
test_write_read "ls_enable: write 0"   ls_enable  0
test_write_read "ls_enable: write 1"   ls_enable  1
test_write_read "rgb_mode: write 0"    rgb_mode   0
test_write_read "rgb_mode: write 1"    rgb_mode   1
test_write_read "sai_ps: write 1"      sai_ps     1
test_write_read "sai_ps: write 0"      sai_ps     0
test_write_read "sai_ls: write 1"      sai_ls     1
test_write_read "sai_ls: write 0"      sai_ls     0

# === Raw reads in RGB mode (rgb_mode=1 after reset) ===

val="$(read_attr in_proximity_raw)"
expect_numeric "PS raw: returns integer" "$val"
expect_in_range "PS raw: within 11-bit range (0-2047)" 0 2047 "$val"

val="$(read_attr in_intensity_red_raw)"
expect_numeric "Intensity RED raw: returns integer" "$val"

val="$(read_attr in_intensity_green_raw)"
expect_numeric "Intensity GREEN raw: returns integer" "$val"

val="$(read_attr in_intensity_blue_raw)"
expect_numeric "Intensity BLUE raw: returns integer" "$val"

val="$(read_attr in_intensity_ir_raw)"
expect_numeric "Intensity IR raw: returns integer" "$val"

# === Switch to ALS mode and read illuminance ===

write_attr rgb_mode 0
sleep 0.2

val="$(read_attr in_illuminance_raw)"
expect_numeric "Illuminance raw in ALS mode" "$val"

val="$(read_attr in_illuminance_input)"
expect_decimal "Illuminance processed in ALS mode" "$val"

write_attr rgb_mode 1
sleep 0.2

# === Hardware gain — all 5 valid values ===

test_write_read "Hardware gain: write 1"   in_intensity_hardwaregain  1
test_write_read "Hardware gain: write 3"   in_intensity_hardwaregain  3
test_write_read "Hardware gain: write 6"   in_intensity_hardwaregain  6
test_write_read "Hardware gain: write 9"   in_intensity_hardwaregain  9
test_write_read "Hardware gain: write 18"  in_intensity_hardwaregain  18

# === Scale ===

val="$(read_attr in_intensity_scale)"
expect_decimal "Intensity scale: returns decimal" "$val"

# === Calibbias ===

test_write_read "Calibbias: write 500"   in_proximity_calibbias  500
test_write_read "Calibbias: write 0"     in_proximity_calibbias  0
test_write_read "Calibbias: write 2047"  in_proximity_calibbias  2047

# === Custom sysfs attributes ===

test_write_read "VCSEL freq: write 80 kHz"         ps_vcsel_freq_khz       80
test_write_read "VCSEL freq: write 100 kHz"        ps_vcsel_freq_khz       100
test_write_read "VCSEL curr: write 10 mA"          ps_vcsel_curr_ma        10
test_write_read "VCSEL curr: write 25 mA"          ps_vcsel_curr_ma        25
test_write_read "PS pulses: write 16"              ps_pulses               16
test_write_read "PS pulses: write 255"             ps_pulses               255
test_write_read "PS reso: write 11 bit"            ps_reso_bit             11
test_write_read "PS reso: write 8 bit"             ps_reso_bit             8
test_write_read "PS meas rate: write 50000 us"     ps_meas_rate_us         50000
test_write_read "PS meas rate: write 100000 us"    ps_meas_rate_us         100000
test_write_read "LS reso: write 18 bit"            ls_reso_bit             18
test_write_read "LS reso: write 20 bit"            ls_reso_bit             20
test_write_read "LS meas rate: write 200 ms"       ls_meas_rate_ms         200
test_write_read "LS meas rate: write 1000 ms"      ls_meas_rate_ms         1000
test_write_read "Analog cancellation: write 15"    ps_analog_cancellation  15
test_write_read "Analog cancellation: write 0"     ps_analog_cancellation  0

# === PS overflow status (read-only, returns 0 or 1) ===

val="$(read_attr ps_overflow)"
expect_in_range "ps_overflow: returns 0 or 1" 0 1 "$val"

# === Available files — verify known values present ===

val="$(read_attr ps_vcsel_freq_khz_available)"
expect_in "vcsel_freq available: contains 70"   "70"  "$val"

val="$(read_attr ps_vcsel_curr_ma_available)"
expect_in "vcsel_curr available: contains 25"   "25"  "$val"

val="$(read_attr ps_pulses_available)"
expect_in "ps_pulses available: [0 1 255]"      "[0 1 255]"  "$val"

val="$(read_attr ps_analog_cancellation_available)"
expect_in "ana_can available: [0 1 31]"         "[0 1 31]"  "$val"

val="$(read_attr ps_reso_bit_available)"
expect_in "ps_reso available: contains 11"      "11"  "$val"

val="$(read_attr ps_meas_rate_us_available)"
expect_in "ps_meas_rate available: contains 100000"  "100000"  "$val"

val="$(read_attr ls_reso_bit_available)"
expect_in "ls_reso available: contains 13"      "13"  "$val"

val="$(read_attr ls_meas_rate_ms_available)"
expect_in "ls_meas_rate available: contains 500"  "500"  "$val"

val="$(read_attr in_intensity_hardwaregain_available)"
expect_in "hwgain available: 1 3 6 9 18"       "1 3 6 9 18"  "$val"

val="$(read_attr in_proximity_calibbias_available)"
expect_in "calibbias available: [0 1 2047]"    "[0 1 2047]"  "$val"

end_suite
