#!/bin/bash
# ---------------------------------------------------------------------------
# test_03_buffer.sh — Triggered buffer data streaming (auto + interactive)
#
# Tests the full hrtimer-trigger → trigger_handler → kfifo → chardev pipeline.
# The iio-trig-hrtimer module provides an automatic trigger that fires at a
# configurable frequency (100 Hz for automatic tests, 5 Hz for interactive).
#
# Automatic phase: verifies that PS-only, ALS+IR, and RGB+IR buffer captures
#   return the expected number of samples with valid values.
#
# Interactive phase: captures longer sample sequences while the user moves
#   objects, cycles lights, and covers/un-covers the sensor.  Validates
#   that the buffered data reflects the physical interaction.
#
# Requires iio_generic_buffer (from linux-tools or kernel tools/iio/).
# If not installed, all buffer data tests are skipped.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

find_device
trap cleanup_dynamic EXIT
reset_sensor

EV="events"
TMPFILE="$(mktemp)"
trap 'rm -f "$TMPFILE"' EXIT

# =================== Pre-flight ===================

begin_suite "Buffer — Pre-flight"

# Check buffer subsystem is present
if [ ! -d "$IIO_DEV/buffer" ]; then
    skip "Buffer subsystem not present (APDS9999_BUFFER not compiled)"
    skip "iio_generic_buffer available"
    skip "Trigger available"
    skip "PS-only capture: 5 samples (auto)"
    skip "PS values valid (auto)"
    skip "ALS+IR capture: 5 samples (auto)"
    skip "RGB+IR capture: 5 samples (auto)"
    skip "Timestamps advance (auto)"
    skip "Buffer cleanup (auto)"
    skip "PS proximity sweep — 5s capture (interactive)"
    skip "ALS mode — 5s capture (interactive)"
    skip "RGB mode — 5s capture (interactive)"
    end_suite
fi

pass "Buffer subsystem present"

# Check iio_generic_buffer
if ! buffer_available; then
    echo -e "  ${__C_YLW}iio_generic_buffer not found on PATH.${__C_RST}"
    echo -e "  ${__C_YLW}Install linux-tools or build from kernel tools/iio/.${__C_RST}"
    skip "iio_generic_buffer available"
    skip "Trigger available"
    skip "PS-only capture: 5 samples (auto)"
    skip "PS values valid (auto)"
    skip "ALS+IR capture: 5 samples (auto)"
    skip "RGB+IR capture: 5 samples (auto)"
    skip "Timestamps advance (auto)"
    skip "Buffer cleanup (auto)"
    skip "PS proximity sweep — 5s capture (interactive)"
    skip "ALS mode — 5s capture (interactive)"
    skip "RGB mode — 5s capture (interactive)"
    end_suite
fi

pass "iio_generic_buffer available"

# Set up sysfs trigger
if ! ensure_sysfs_trigger; then
    skip "Trigger available (no sysfs trigger)"
    skip "PS-only capture: 5 samples (auto)"
    skip "PS values valid (auto)"
    skip "ALS+IR capture: 5 samples (auto)"
    skip "RGB+IR capture: 5 samples (auto)"
    skip "Timestamps advance (auto)"
    skip "Buffer cleanup (auto)"
    skip "PS proximity sweep — 5s capture (interactive)"
    skip "ALS mode — 5s capture (interactive)"
    skip "RGB mode — 5s capture (interactive)"
    end_suite
fi

pass "Trigger available"

# Make sure buffer is off and scan elements are clean
ensure_buffer_off
disable_all_scan_elements

# =================== Automatic Phase ===================

begin_suite "Buffer — Automatic"

# --- PS-only: capture 5 samples ---

write_attr scan_elements/in_proximity_en 1

capture_buffer_samples 5 300 "$TMPFILE"
capture_exit_code=$?
if [ $capture_exit_code -eq 0 ]; then

    line_count="$(count_buffer_lines "$TMPFILE")"

    if [ "$line_count" -ge 5 ]; then
        pass "PS-only capture: 5 samples ($line_count lines)"
    else
        fail "PS-only capture: expected ≥5 lines, got $line_count"
    fi

    # Validate PS values (first column should be 0–2047)
    ps_out_of_range=false
    while IFS= read -r line; do
        ps_val="$(echo "$line" | awk '{print $1}')"
        if echo "$ps_val" | grep -qE '^[0-9]+$'; then
            if [ "$ps_val" -gt 2047 ]; then
                ps_out_of_range=true
                break
            fi
        fi
    done < <(grep -E '^[0-9 ]' "$TMPFILE")

    if $ps_out_of_range; then
        fail "PS values valid: found value > 2047"
    else
        pass "PS values valid: all in 0–2047"
    fi
else
    fail "PS-only capture: iio_generic_buffer failed: exit code $capture_exit_code"
    skip "PS values valid"
fi

ensure_buffer_off
disable_all_scan_elements

# --- ALS+IR: capture 5 samples ---

write_attr rgb_mode 0
sleep 0.3

write_attr scan_elements/in_illuminance_en 1
write_attr scan_elements/in_intensity_ir_en 1
capture_buffer_samples 5 300 "$TMPFILE"
capture_exit_code=$?

if [ $capture_exit_code -eq 0 ]; then
    line_count="$(count_buffer_lines "$TMPFILE")"

    if [ "$line_count" -ge 5 ]; then
        pass "ALS+IR capture: 5 samples ($line_count lines)"
    else
        fail "ALS+IR capture: expected ≥5 lines, got $line_count"
    fi

    # Check that values are non-negative
    als_negative=false
    while IFS= read -r line; do
        als_val="$(echo "$line" | awk '{print $1}')"
        if echo "$als_val" | grep -qE '^-'; then
            als_negative=true
            break
        fi
    done < <(grep -E '^[0-9 ]' "$TMPFILE")

    if $als_negative; then
        fail "ALS+IR values: found negative value"
    else
        pass "ALS+IR values: all non-negative"
    fi
else
    fail "ALS+IR capture: iio_generic_buffer failed: exit code $capture_exit_code"
    skip "ALS+IR values"
fi

ensure_buffer_off
disable_all_scan_elements
write_attr rgb_mode 1
sleep 0.3

# --- RGB+IR: capture 5 samples ---

write_attr scan_elements/in_intensity_red_en   1
write_attr scan_elements/in_intensity_green_en 1
write_attr scan_elements/in_intensity_blue_en  1
write_attr scan_elements/in_intensity_ir_en    1
capture_buffer_samples 5 300 "$TMPFILE"
capture_exit_code=$?

if [ $capture_exit_code -eq 0 ]; then
    line_count="$(count_buffer_lines "$TMPFILE")"

    if [ "$line_count" -ge 5 ]; then
        pass "RGB+IR capture: 5 samples ($line_count lines)"
    else
        fail "RGB+IR capture: expected ≥5 lines, got $line_count"
    fi
else
    fail "RGB+IR capture: iio_generic_buffer failed: exit code $capture_exit_code"
fi

ensure_buffer_off
disable_all_scan_elements

# --- Timestamp check: use the last PS capture output ---
# iio_generic_buffer prints timestamps as the last value on each line.
# We check that the last column increases across samples.

write_attr scan_elements/in_proximity_en 1
capture_buffer_samples 5 300 "$TMPFILE"
capture_exit_code=$?

if [ $capture_exit_code -eq 0 ]; then
    ts_monotonic=true
    prev_ts=""
    while IFS= read -r line; do
        # Timestamp is typically the last space-separated field
        ts="$(echo "$line" | awk '{print $NF}')"
        if [ -n "$prev_ts" ] && echo "$ts" | grep -qE '^-?[0-9]+$' && echo "$prev_ts" | grep -qE '^-?[0-9]+$'; then
            if [ "$ts" -le "$prev_ts" ] 2>/dev/null; then
                ts_monotonic=false
                break
            fi
        fi
        prev_ts="$ts"
    done < <(grep -E '^[0-9 ]' "$TMPFILE")

    if $ts_monotonic; then
        pass "Timestamps advance: monotonically increasing"
    else
        fail "Timestamps advance: not monotonically increasing"
    fi
else
    skip "Timestamps advance (capture failed: exit code $capture_exit_code)"
fi

ensure_buffer_off
disable_all_scan_elements

# --- Buffer cleanup ---

val="$(read_attr buffer/enable)"
if [ "$val" = "0" ]; then
    pass "Buffer cleanup: buffer disabled"
else
    fail "Buffer cleanup: buffer/enable = $val (expected 0)"
fi

echo ""

# =================== Interactive Phase ===================

begin_suite "Buffer — Interactive"

if ! interactive_enabled; then
    skip "PS proximity sweep — 5s capture (interactive — set APDS9999_INTERACTIVE=1)"
    skip "ALS mode — 5s capture (interactive)"
    skip "RGB mode — 5s capture (interactive)"
    end_suite
fi

# Sysfs trigger is manually fired — no frequency to set.
# Each capture fires the trigger once per sample in a loop.
# 5 seconds of capture at ~20 samples/sec = 100 samples.
INTERACTIVE_SAMPLES=100

# --- Stage 1: PS proximity sweep — 5s capture ---
#
# User moves hand from far to close and back.
# We check that the range (max - min) is significant.
# On failure the user can type 'r' to retry.

stage_ps_done=false
while ! $stage_ps_done; do
    ensure_buffer_off
    disable_all_scan_elements
    write_attr scan_elements/in_proximity_en 1

    if prompt_user "Move your hand from far to close and back over the sensor. 5 seconds of data will be captured."; then
        capture_buffer_samples "$INTERACTIVE_SAMPLES" 0 "$TMPFILE"
        capture_exit_code=$?

        if [ $capture_exit_code -eq 0 ]; then
            # Extract PS values (first column) and compute range
            ps_vals="$(grep -E '^[0-9 ]' "$TMPFILE" | awk '{printf "%d\n", $1}')"
            ps_min="$(echo "$ps_vals" | sort -n | head -1)"
            ps_max="$(echo "$ps_vals" | sort -n | tail -1)"
            ps_range=$(( ps_max - ps_min ))

            echo -e "    PS sweep: min=$ps_min  max=$ps_max  range=$ps_range"

            # Sparkline
            spark="$(sparkline "$ps_min" "$ps_max" $ps_vals)"
            echo -e "    ${__C_BLD}$spark${__C_RST}"

            if [ "$ps_range" -gt 200 ]; then
                pass "PS proximity sweep: range > 200 ($ps_range)"
                stage_ps_done=true
            else
                fail "PS proximity sweep: range too small ($ps_range — move hand more!)"
            fi
        else
            fail "PS proximity sweep: capture failed: exit code $capture_exit_code"
        fi
    else
        skip "PS proximity sweep (user skipped)"
        stage_ps_done=true
    fi

    if ! $stage_ps_done; then
        echo -e "  ${__C_YLW}Type 'r' to retry, or press ENTER to skip...${__C_RST}"
        read -r -p "    " retry_reply </dev/tty
        if [ "$retry_reply" != "r" ] && [ "$retry_reply" != "R" ]; then
            skip "PS proximity sweep (user chose not to retry)"
            stage_ps_done=true
        fi
    fi
done

ensure_buffer_off
disable_all_scan_elements

# --- Stage 2: ALS mode (ALS + IR) — 5s capture ---
#
# User places sensor under ambient light.
# iio_generic_buffer outputs columns in scan_index order: ALS (idx 5), IR (idx 4).
# We validate that ALS values are non-negative.
# On failure the user can type 'r' to retry.

stage_als_done=false
while ! $stage_als_done; do
    ensure_buffer_off
    disable_all_scan_elements

    write_attr rgb_mode 0
    sleep 0.3

    write_attr scan_elements/in_illuminance_en 1
    write_attr scan_elements/in_intensity_ir_en 1

    if prompt_user "Place the sensor under ambient light. 5 seconds of ALS + IR data will be captured."; then
        capture_buffer_samples "$INTERACTIVE_SAMPLES" 0 "$TMPFILE"
        capture_exit_code=$?

        if [ $capture_exit_code -eq 0 ]; then
            line_count="$(count_buffer_lines "$TMPFILE")"

            if [ "$line_count" -ge 10 ]; then
                # Print ALS and IR values
                echo -e "    ${__C_BLD}Sample  ALS       IR${__C_RST}"
                grep -E '^[0-9 ]' "$TMPFILE" | head -20 | awk '
                {
                    als = $1; ir = $2
                    printf "    #%-3d   %-9d %d\n", NR, als, ir
                }'

                # Compute stats
                als_vals="$(grep -E '^[0-9 ]' "$TMPFILE" | awk '{printf "%d\n", $1}')"
                ir_vals="$(grep -E '^[0-9 ]' "$TMPFILE" | awk '{printf "%d\n", $2}')"
                als_min="$(echo "$als_vals" | sort -n | head -1)"
                als_max="$(echo "$als_vals" | sort -n | tail -1)"
                ir_min="$(echo "$ir_vals" | sort -n | head -1)"
                ir_max="$(echo "$ir_vals" | sort -n | tail -1)"

                echo -e "    ALS: min=$als_min  max=$als_max    IR: min=$ir_min  max=$ir_max"

                # Sparkline for ALS
                spark="$(sparkline "$als_min" "$als_max" $als_vals)"
                echo -e "    ${__C_BLD}ALS: $spark${__C_RST}"

                # Check that ALS values are non-negative
                als_negative=false
                while IFS= read -r v; do
                    if [ "$v" -le 0 ] 2>/dev/null; then
                        als_negative=true
                        break
                    fi
                done <<< "$als_vals"

                if $als_negative; then
                    fail "ALS mode: found <= 0 ALS value"
                else
                    pass "ALS mode — 5s capture: ALS + IR ($line_count samples)"
                    stage_als_done=true
                fi
            else
                fail "ALS mode — too few samples ($line_count)"
            fi
        else
            fail "ALS mode — capture failed: exit code $capture_exit_code"
        fi
    else
        skip "ALS mode (user skipped)"
        stage_als_done=true
    fi

    if ! $stage_als_done; then
        echo -e "  ${__C_YLW}Type 'r' to retry, or press ENTER to skip...${__C_RST}"
        read -r -p "    " retry_reply </dev/tty
        if [ "$retry_reply" != "r" ] && [ "$retry_reply" != "R" ]; then
            skip "ALS mode (user chose not to retry)"
            stage_als_done=true
        fi
    fi
done

ensure_buffer_off
disable_all_scan_elements

# --- Stage 3: RGB mode (R + G + B + IR) — 5s capture ---
#
# User cycles red → green → blue light.
# iio_generic_buffer outputs columns in scan_index order: R (idx 1), G (idx 2), B (idx 3), IR (idx 4).
# We check that the dominant channel changes at least 2 times.
# On failure the user can type 'r' to retry.

stage_rgb_done=false
while ! $stage_rgb_done; do
    ensure_buffer_off
    disable_all_scan_elements

    write_attr rgb_mode 1
    sleep 0.3

    write_attr scan_elements/in_intensity_red_en   1
    write_attr scan_elements/in_intensity_green_en 1
    write_attr scan_elements/in_intensity_blue_en  1
    write_attr scan_elements/in_intensity_ir_en    1

    if prompt_user "Cycle through RED → GREEN → BLUE lights over the sensor. 5 seconds of RGB + IR data will be captured."; then
        capture_buffer_samples "$INTERACTIVE_SAMPLES" 0 "$TMPFILE"
        capture_exit_code=$?

        if [ $capture_exit_code -eq 0 ]; then
            line_count="$(count_buffer_lines "$TMPFILE")"

            if [ "$line_count" -ge 10 ]; then
                # Determine dominant channel per sample and count transitions
                result="$(awk '
                /^[0-9 ]/ {
                    r = $1; g = $2; b = $3
                    if (r >= g && r >= b) dom = "R"
                    else if (g >= r && g >= b) dom = "G"
                    else dom = "B"

                    if (dom != prev_dom && prev_dom != "") transitions++
                    prev_dom = dom
                }
                END { print transitions+0 }
                ' "$TMPFILE")"

                echo -e "    RGB sweep: $result dominant-channel transitions"

                # Print a compact table with IR
                echo -e "    ${__C_BLD}Sample  R         G         B         IR${__C_RST}"
                grep -E '^[0-9 ]' "$TMPFILE" | head -20 | awk '
                {
                    r = $1; g = $2; b = $3; ir = $4
                    if (r >= g && r >= b) dom = "RED"
                    else if (g >= r && g >= b) dom = "GREEN"
                    else dom = "BLUE"
                    printf "    #%-3d   %-9d %-9d %-9d %-6d %s\n", NR, r, g, b, ir, dom
                }'

                if [ "$result" -ge 2 ]; then
                    pass "RGB mode — 5s capture: ≥2 transitions ($result)"
                    stage_rgb_done=true
                else
                    fail "RGB mode — too few transitions ($result — cycle the lights!)"
                fi
            else
                fail "RGB mode — too few samples ($line_count)"
            fi
        else
            fail "RGB mode — capture failed: exit code $capture_exit_code"
        fi
    else
        skip "RGB mode (user skipped)"
        stage_rgb_done=true
    fi

    if ! $stage_rgb_done; then
        echo -e "  ${__C_YLW}Type 'r' to retry, or press ENTER to skip...${__C_RST}"
        read -r -p "    " retry_reply </dev/tty
        if [ "$retry_reply" != "r" ] && [ "$retry_reply" != "R" ]; then
            skip "RGB mode (user chose not to retry)"
            stage_rgb_done=true
        fi
    fi
done

ensure_buffer_off
disable_all_scan_elements

end_suite
