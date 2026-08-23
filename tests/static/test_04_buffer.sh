#!/bin/bash
# ---------------------------------------------------------------------------
# test_buffer.sh — Triggered buffer configuration and validation tests
#
# Covers: buffer subsystem presence, scan element files, trigger setup,
# buffer enable/disable lifecycle, and preenable mode validation
# (rejecting incompatible scan mask + rgb_mode combinations).
#
# If the driver was compiled without APDS9999_BUFFER, all tests are skipped.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib.sh"

find_device
trap reset_sensor EXIT
reset_sensor

begin_suite "Buffer"

# === Check if buffer subsystem is present ===

if [ ! -d "$IIO_DEV/buffer" ]; then
    skip "Buffer subsystem not present (APDS9999_BUFFER not compiled)"
    skip "scan_elements"
    skip "PS-only buffer enable"
    skip "ALS+IR buffer in ALS mode"
    skip "RGB+IR buffer in RGB mode"
    skip "preenable: ALS scan in RGB mode"
    skip "preenable: RGB scan in ALS mode"
    skip "buffer disable cleanup"
    end_suite
fi

pass "Buffer subsystem present"

# === scan_elements exist ===

scan_ok=true
for ch in in_proximity_en in_intensity_red_en in_intensity_green_en \
          in_intensity_blue_en in_intensity_ir_en in_illuminance_en; do
    if [ ! -f "$IIO_DEV/scan_elements/$ch" ]; then
        fail "scan_elements/$ch missing"
        scan_ok=false
    fi
done
if $scan_ok; then
    pass "All scan_elements present"
fi

# === Helper: disable all scan elements ===
disable_all_scan_elements() {
    for ch in in_proximity_en in_intensity_red_en in_intensity_green_en \
              in_intensity_blue_en in_intensity_ir_en in_illuminance_en; do
        write_attr "scan_elements/$ch" 0 2>/dev/null || true
    done
}

# === Helper: ensure buffer is disabled ===
ensure_buffer_off() {
    write_attr buffer/enable 0 2>/dev/null || true
}

ensure_buffer_off
disable_all_scan_elements

# === Set up a sysfs trigger (required for INDIO_BUFFER_TRIGGERED) ===

modprobe iio-trig-sysfs 2>/dev/null || true
if [ ! -d /sys/bus/iio/devices/trigger0 ]; then
    echo 1 > /sys/bus/iio/devices/iio_sysfs_trigger/add_trigger 2>/dev/null || true
fi

TRIG_NAME=""
for trig in /sys/bus/iio/devices/trigger*; do
    [ -d "$trig" ] || continue
    TRIG_NAME="$(cat "$trig/name" 2>/dev/null)"
    if [ -n "$TRIG_NAME" ]; then
        break
    fi
done

if [ -z "$TRIG_NAME" ]; then
    skip "PS-only buffer enable (no trigger available)"
    skip "buffer/enable reads 1"
    skip "buffer/enable reads 0 after disable"
    skip "ALS+IR buffer in ALS mode (no trigger available)"
    skip "RGB+IR buffer in RGB mode (no trigger available)"
    skip "preenable: ALS scan in RGB mode (no trigger available)"
    skip "preenable: RGB scan in ALS mode (no trigger available)"
    pass "buffer disable cleanup"
    end_suite
fi

write_attr trigger/current_trigger "$TRIG_NAME"

# === PS-only buffer enable/disable ===

write_attr scan_elements/in_proximity_en 1
if write_attr buffer/enable 1; then
    pass "PS-only buffer enable"
    val="$(read_attr buffer/enable)"
    expect_eq "buffer/enable reads 1" "1" "$val"
    write_attr buffer/enable 0
    val="$(read_attr buffer/enable)"
    expect_eq "buffer/enable reads 0 after disable" "0" "$val"
else
    fail "PS-only buffer enable"
    fail "buffer/enable reads 1"
    fail "buffer/enable reads 0 after disable"
fi
disable_all_scan_elements

# === ALS+IR buffer in ALS mode (rgb_mode=0) ===

write_attr rgb_mode 0
sleep 0.2
write_attr scan_elements/in_illuminance_en 1
write_attr scan_elements/in_intensity_ir_en 1
if write_attr buffer/enable 1; then
    pass "ALS+IR buffer enable in ALS mode"
    write_attr buffer/enable 0
else
    fail "ALS+IR buffer enable in ALS mode"
fi
disable_all_scan_elements

# === RGB+IR buffer in RGB mode (rgb_mode=1) ===

write_attr rgb_mode 1
sleep 0.2
write_attr scan_elements/in_intensity_red_en 1
write_attr scan_elements/in_intensity_green_en 1
write_attr scan_elements/in_intensity_blue_en 1
write_attr scan_elements/in_intensity_ir_en 1
if write_attr buffer/enable 1; then
    pass "RGB+IR buffer enable in RGB mode"
    write_attr buffer/enable 0
else
    fail "RGB+IR buffer enable in RGB mode"
fi
disable_all_scan_elements

# === preenable rejection: ALS scan mask while in RGB mode ===

write_attr rgb_mode 1
sleep 0.2
write_attr scan_elements/in_illuminance_en 1
write_attr scan_elements/in_intensity_ir_en 1
if write_attr buffer/enable 1; then
    fail "preenable: ALS scan in RGB mode should be rejected"
    write_attr buffer/enable 0
else
    pass "preenable: ALS scan in RGB mode rejected"
fi
disable_all_scan_elements

# === preenable rejection: RGB scan mask while in ALS mode ===

write_attr rgb_mode 0
sleep 0.2
write_attr scan_elements/in_intensity_red_en 1
write_attr scan_elements/in_intensity_green_en 1
write_attr scan_elements/in_intensity_blue_en 1
write_attr scan_elements/in_intensity_ir_en 1
if write_attr buffer/enable 1; then
    fail "preenable: RGB scan in ALS mode should be rejected"
    write_attr buffer/enable 0
else
    pass "preenable: RGB scan in ALS mode rejected"
fi
disable_all_scan_elements

# === Cleanup ===

ensure_buffer_off
pass "buffer disable cleanup"

end_suite
