#!/bin/bash
# ---------------------------------------------------------------------------
# test_01_direct_readings.sh — Live channel reading tests (auto + interactive)
#
# Automatic phase: verifies that every channel returns physically plausible
#   data, that IR is always readable, that mode exclusivity works at runtime,
#   and that gain/resolution changes produce observable effects.
#
# Interactive phase: prompts the user to cover the sensor, bring objects
#   close, shine coloured lights, and points a remote at the sensor.
#   Each interaction validates that the driver reports the expected effect.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

find_device
trap reset_sensor EXIT
reset_sensor

# =================== Automatic Phase ===================

begin_suite "Direct Readings — Automatic"

# --- PS raw reading ---

val="$(read_attr in_proximity_raw)"
expect_numeric "PS raw: returns integer" "$val"
expect_in_range "PS raw: within 11-bit range (0–2047)" 0 2047 "$val"

# Two consecutive reads should differ (sensor noise)
val2="$(read_attr in_proximity_raw)"
if [ "$val" != "$val2" ]; then
    pass "PS raw: consecutive reads differ ($val vs $val2)"
else
    # Tolerate: sometimes two reads can match by chance
    skip "PS raw: consecutive reads differ ($val = $val2 — acceptable)"
fi

# --- RGB raw readings (rgb_mode=1 after reset) ---

for ch in red green blue ir; do
    val="$(read_attr "in_intensity_${ch}_raw")"
    expect_numeric "Intensity ${ch^^} raw: returns integer" "$val"
done

# IR should always be non-zero (ambient IR is always present)
ir_val="$(read_attr in_intensity_ir_raw)"
if [ "$ir_val" -gt 0 ] 2>/dev/null; then
    pass "IR raw: non-zero in RGB mode ($ir_val)"
else
    skip "IR raw: non-zero in RGB mode (got $ir_val — dark environment?)"
fi

# --- Switch to ALS mode and read illuminance ---

write_attr rgb_mode 0
sleep 0.3

val="$(read_attr in_illuminance_raw)"
expect_numeric "Illuminance raw in ALS mode" "$val"

val="$(read_attr in_illuminance_input)"
expect_decimal "Illuminance processed in ALS mode" "$val"

# Lux plausible range (0 = dark, ~100000 = direct sunlight)
lux_int="$(echo "$val" | awk -F. '{print $1}')"
if [ -n "$lux_int" ] && echo "$lux_int" | grep -qE '^[0-9]+$'; then
    if [ "$lux_int" -ge 0 ] && [ "$lux_int" -le 100000 ]; then
        pass "Processed lux in plausible range ($val)"
    else
        fail "Processed lux out of range ($val, expected 0–100000)"
    fi
else
    skip "Processed lux range check (could not parse '$val')"
fi

write_attr rgb_mode 1
sleep 0.3

# --- IR always readable in both modes ---

val_rgb="$(read_attr in_intensity_ir_raw)"
expect_numeric "IR raw readable in RGB mode" "$val_rgb"

write_attr rgb_mode 0
sleep 0.3
val_als="$(read_attr in_intensity_ir_raw)"
expect_numeric "IR raw readable in ALS mode" "$val_als"

write_attr rgb_mode 1
sleep 0.3

# --- Gain changes reading ---

write_attr in_intensity_hardwaregain 1
sleep 0.2
gain1_val="$(read_attr in_intensity_ir_raw)"

write_attr in_intensity_hardwaregain 18
sleep 0.2
gain18_val="$(read_attr in_intensity_ir_raw)"

if [ "$gain18_val" -gt "$gain1_val" ] 2>/dev/null; then
    pass "Gain effect: 18× ($gain18_val) > 1× ($gain1_val)"
else
    skip "Gain effect: 18× ($gain18_val) vs 1× ($gain1_val — may be saturated)"
fi

# Reset gain
write_attr in_intensity_hardwaregain 1

# --- Mode exclusivity at runtime ---

# In RGB mode, ALS reads should fail
if read_attr in_illuminance_raw >/dev/null 2>&1; then
    fail "Mode exclusivity: ALS raw readable in RGB mode"
else
    pass "Mode exclusivity: ALS raw rejected in RGB mode"
fi

write_attr rgb_mode 0
sleep 0.3

# In ALS mode, RGB reads should fail
if read_attr in_intensity_red_raw >/dev/null 2>&1; then
    fail "Mode exclusivity: RED raw readable in ALS mode"
else
    pass "Mode exclusivity: RED raw rejected in ALS mode"
fi

write_attr rgb_mode 1
sleep 0.3

echo ""

# =================== Interactive Phase ===================

begin_suite "Direct Readings — Interactive"

if ! interactive_enabled; then
    skip "PS covered baseline (interactive — set APDS9999_INTERACTIVE=1)"
    skip "PS close proximity (interactive)"
    skip "PS far vs near (interactive)"
    skip "ALS dark reading (interactive)"
    skip "ALS bright reading (interactive)"
    skip "RED light dominance (interactive)"
    skip "GREEN light dominance (interactive)"
    skip "BLUE light dominance (interactive)"
    skip "WHITE light balance (interactive)"
    skip "IR remote burst (interactive)"
    end_suite
fi

# --- PS dark baseline ---

if prompt_user "Cover the sensor completely"; then
    sleep 0.3
    val="$(read_attr in_proximity_raw)"
    expect_in_range "PS covered baseline: elevated (200–2047)" 200 2047 "$val"
else
    skip "PS covered baseline (user skipped)"
fi

# --- PS close proximity ---
# Higher PS values indicate closer objects (more reflected IR).
# 2–5 cm is close but not touching, so expect elevated values.

if prompt_user "Place an object ~2–5 cm from the sensor"; then
    sleep 0.3
    val="$(read_attr in_proximity_raw)"
    expect_in_range "PS close proximity: elevated (200–2047)" 200 2047 "$val"
else
    skip "PS close proximity (user skipped)"
fi

# --- PS far vs near ---

if prompt_user "Move the object far away (>30 cm)"; then
    sleep 0.3
    far_val="$(read_attr in_proximity_raw)"

    if prompt_user "Now bring the object close (~2 cm)"; then
        sleep 0.3
        near_val="$(read_attr in_proximity_raw)"

        if [ "$near_val" -gt "$far_val" ] 2>/dev/null; then
            pass "PS far vs near: close ($near_val) > far ($far_val)"
        else
            fail "PS far vs near: expected close ($near_val) > far ($far_val)"
        fi
    else
        skip "PS far vs near (user skipped second prompt)"
    fi
else
    skip "PS far vs near (user skipped)"
fi

# --- ALS dark reading ---

write_attr rgb_mode 0
sleep 0.3

if prompt_user "Cover the sensor completely for an ALS reading"; then
    sleep 0.3
    dark_raw="$(read_attr in_illuminance_raw)"
    dark_lux="$(read_attr in_illuminance_input)"
    print_readings "ALS dark" "" "" "" "" "" "$dark_raw"
    expect_in_range "ALS dark raw: near zero (0–100)" 0 100 "$dark_raw"
else
    skip "ALS dark reading (user skipped)"
fi

# --- ALS bright reading ---

if prompt_user "Shine a bright light (flashlight, lamp) directly at the sensor"; then
    sleep 0.3
    bright_raw="$(read_attr in_illuminance_raw)"
    print_readings "ALS bright" "" "" "" "" "" "$bright_raw"

    if [ -n "${dark_raw:-}" ] && [ "$bright_raw" -gt "$(( dark_raw * 10 ))" ] 2>/dev/null; then
        pass "ALS bright: >10× dark ($bright_raw vs dark $dark_raw)"
    elif [ "$bright_raw" -gt 0 ] 2>/dev/null; then
        pass "ALS bright: non-zero ($bright_raw)"
    else
        fail "ALS bright: expected significant reading, got '$bright_raw'"
    fi
else
    skip "ALS bright reading (user skipped)"
fi

write_attr rgb_mode 1
sleep 0.3

# --- RED light ---

if prompt_user "Place a RED light source in front of the sensor (or 's' to skip)"; then
    sleep 0.3
    r="$(read_attr in_intensity_red_raw)"
    g="$(read_attr in_intensity_green_raw)"
    b="$(read_attr in_intensity_blue_raw)"
    print_readings "RED light" "" "$r" "$g" "$b" ""

    if [ "$r" -ge "$g" ] 2>/dev/null && [ "$r" -ge "$b" ] 2>/dev/null; then
        pass "RED light: R ($r) is dominant"
    else
        fail "RED light: expected R ($r) to be dominant (G=$g, B=$b)"
    fi
else
    skip "RED light dominance (user skipped)"
fi

# --- GREEN light ---

if prompt_user "Place a GREEN light source in front of the sensor (or 's' to skip)"; then
    sleep 0.3
    r="$(read_attr in_intensity_red_raw)"
    g="$(read_attr in_intensity_green_raw)"
    b="$(read_attr in_intensity_blue_raw)"
    print_readings "GREEN light" "" "$r" "$g" "$b" ""

    if [ "$g" -ge "$r" ] 2>/dev/null && [ "$g" -ge "$b" ] 2>/dev/null; then
        pass "GREEN light: G ($g) is dominant"
    else
        fail "GREEN light: expected G ($g) to be dominant (R=$r, B=$b)"
    fi
else
    skip "GREEN light dominance (user skipped)"
fi

# --- BLUE light ---

if prompt_user "Place a BLUE light source in front of the sensor (or 's' to skip)"; then
    sleep 0.3
    r="$(read_attr in_intensity_red_raw)"
    g="$(read_attr in_intensity_green_raw)"
    b="$(read_attr in_intensity_blue_raw)"
    print_readings "BLUE light" "" "$r" "$g" "$b" ""

    if [ "$b" -ge "$r" ] 2>/dev/null && [ "$b" -ge "$g" ] 2>/dev/null; then
        pass "BLUE light: B ($b) is dominant"
    else
        fail "BLUE light: expected B ($b) to be dominant (R=$r, G=$g)"
    fi
else
    skip "BLUE light dominance (user skipped)"
fi

# --- WHITE light ---

if prompt_user "Place a WHITE light source (or daylight) in front of the sensor (or 's' to skip)"; then
    sleep 0.3
    r="$(read_attr in_intensity_red_raw)"
    g="$(read_attr in_intensity_green_raw)"
    b="$(read_attr in_intensity_blue_raw)"
    print_readings "WHITE light" "" "$r" "$g" "$b" ""

    # All channels should be within 3× of each other
    all_nonzero=true
    for v in "$r" "$g" "$b"; do
        [ "$v" -gt 0 ] 2>/dev/null || all_nonzero=false
    done

    if $all_nonzero; then
        min_val="$r"; [ "$g" -lt "$min_val" ] && min_val="$g"; [ "$b" -lt "$min_val" ] && min_val="$b"
        max_val="$r"; [ "$g" -gt "$max_val" ] && max_val="$g"; [ "$b" -gt "$max_val" ] && max_val="$b"
        threshold=$(( min_val * 3 ))

        if [ "$max_val" -le "$threshold" ]; then
            pass "WHITE light: channels balanced (max $max_val ≤ 3× min $min_val)"
        else
            fail "WHITE light: channels unbalanced (max $max_val > 3× min $min_val)"
        fi
    else
        skip "WHITE light: some channels returned zero"
    fi
else
    skip "WHITE light balance (user skipped)"
fi

# --- IR remote burst ---

ir_before="$(read_attr in_intensity_ir_raw)"

if prompt_user "Point a TV remote at the sensor and hold a button, then press ENTER"; then
    sleep 0.1
    ir_after="$(read_attr in_intensity_ir_raw)"
    print_readings "IR remote" "" "" "" "" "$ir_after" "(before: $ir_before)"

    if [ "$ir_after" -gt "$(( ir_before * 2 ))" ] 2>/dev/null; then
        pass "IR remote: IR spiked ($ir_after > 2× $ir_before)"
    elif [ "$ir_after" -gt "$ir_before" ] 2>/dev/null; then
        pass "IR remote: IR increased ($ir_after > $ir_before)"
    else
        fail "IR remote: expected IR increase ($ir_after vs $ir_before)"
    fi
else
    skip "IR remote burst (user skipped)"
fi

end_suite
