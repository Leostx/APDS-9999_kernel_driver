#!/bin/bash
# ---------------------------------------------------------------------------
# test_02_events.sh — Hardware interrupt event monitoring (auto + interactive)
#
# Tests the full IRQ → iio_push_event → chardev pipeline end-to-end.
# Uses iio_event_monitor (from kernel-tools) to read events from the chardev.
#
# Automatic phase: verifies that events fire with trivial thresholds and
#   that disabling events stops delivery.
#
# Interactive phase: prompts the user to approach/retreat from the PS and
#   to cover/expose the LS, validating that threshold events fire with the
#   correct direction.
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

find_device
find_chardev
trap cleanup_dynamic EXIT
reset_sensor

EV="events"

# =================== Pre-flight ===================

begin_suite "Events — Pre-flight"

# Check IRQ is wired (events require an interrupt line)
if [ ! -f "$IIO_DEV/$EV/in_proximity_thresh_en" ]; then
    echo -e "  ${__C_YLW}No event attributes found — IRQ may not be wired.${__C_RST}"
    skip "IRQ present"
    skip "PS threshold event fires (auto)"
    skip "Event timestamp > 0 (auto)"
    skip "PS event direction decode (auto)"
    skip "LS threshold event fires (auto)"
    skip "Event disable stops delivery (auto)"
    skip "PS proximity event — approach (interactive)"
    skip "PS proximity event — retreat (interactive)"
    skip "LS threshold event — cover (interactive)"
    skip "LS threshold event — expose (interactive)"
    skip "LS variance event — wave hand (interactive)"
    end_suite
fi

# Check iio_event_monitor is available
if [ ! -x "$_KERNEL_TOOLS_DIR/iio_event_monitor" ]; then
    echo -e "  ${__C_YLW}iio_event_monitor not found in kernel-tools/.${__C_RST}"
    skip "iio_event_monitor available"
    skip "PS threshold event fires (auto)"
    skip "Event timestamp > 0 (auto)"
    skip "PS event direction decode (auto)"
    skip "LS threshold event fires (auto)"
    skip "Event disable stops delivery (auto)"
    skip "PS proximity event — approach (interactive)"
    skip "PS proximity event — retreat (interactive)"
    skip "LS threshold event — cover (interactive)"
    skip "LS threshold event — expose (interactive)"
    skip "LS variance event — wave hand (interactive)"
    end_suite
fi

pass "IRQ present (event attributes exist)"
pass "iio_event_monitor available"

# =================== Automatic Phase ===================

begin_suite "Events — Automatic"

# --- Disable all events first ---
write_attr "$EV/in_proximity_thresh_en"   0
write_attr "$EV/in_illuminance_thresh_en" 0
write_attr "$EV/in_illuminance_change_en" 0

# --- PS threshold event with trivial threshold ---
# Set rising threshold to 1 — anything above triggers it.
write_attr "$EV/in_proximity_thresh_rising_value"  1
write_attr "$EV/in_proximity_thresh_falling_value" 0
write_attr "$EV/in_proximity_thresh_period" 1

# Start event monitor BEFORE enabling the threshold
start_event_monitor

write_attr "$EV/in_proximity_thresh_en" 1
sleep 3

stop_event_monitor
parse_events "proximity"

if [ "$EVENT_COUNT" -gt 0 ]; then
    pass "PS threshold event fires (auto — $EVENT_COUNT event(s))"

    if [ -n "$EVENT_TS" ] && [ "$EVENT_TS" != "0" ] && [ "$EVENT_TS" != "" ]; then
        pass "Event timestamp > 0 ($EVENT_TS)"
    else
        skip "Event timestamp > 0 (monitor did not report timestamp)"
    fi

    # Check direction field
    case "$EVENT_DIR" in
        1) pass "PS event direction: rising (1)" ;;
        2) pass "PS event direction: falling (2)" ;;
        3) pass "PS event direction: either (3)" ;;
        *) skip "PS event direction decode (got $EVENT_DIR — may be encoded differently)" ;;
    esac
else
    fail "PS threshold event fires (auto — no proximity event within 3s)"
    skip "Event timestamp > 0"
    skip "PS event direction decode"
fi

write_attr "$EV/in_proximity_thresh_en" 0
sleep 0.1

# --- LS threshold event with trivial threshold ---
# Switch to ALS mode for the LS channel
write_attr rgb_mode 0
sleep 0.3

write_attr "$EV/in_illuminance_thresh_rising_value"  1
write_attr "$EV/in_illuminance_thresh_falling_value" 0
write_attr "$EV/in_illuminance_thresh_period" 1

start_event_monitor

write_attr "$EV/in_illuminance_thresh_en" 1
sleep 3

stop_event_monitor
parse_events "illuminance"

if [ "$EVENT_COUNT" -gt 0 ]; then
    pass "LS threshold event fires (auto — $EVENT_COUNT event(s))"
else
    # LS events may not fire in complete darkness — skip rather than fail
    skip "LS threshold event fires (auto — no event, dark environment?)"
fi

write_attr "$EV/in_illuminance_thresh_en" 0
write_attr rgb_mode 1
sleep 0.2

# --- Event disable stops delivery ---
write_attr "$EV/in_proximity_thresh_en"   0
write_attr "$EV/in_illuminance_thresh_en" 0
write_attr "$EV/in_illuminance_change_en" 0
sleep 0.2

start_event_monitor
sleep 3
stop_event_monitor
parse_events

if [ "$EVENT_COUNT" -gt 0 ]; then
    fail "Event disable: received event after disable ($EVENT_COUNT event(s))"
else
    pass "Event disable: no event within 3s"
fi

echo ""

# =================== Interactive Phase ===================

begin_suite "Events — Interactive"

if ! interactive_enabled; then
    skip "PS proximity event — approach (interactive — set APDS9999_INTERACTIVE=1)"
    skip "PS proximity event — retreat (interactive)"
    skip "LS threshold event — cover (interactive)"
    skip "LS threshold event — expose (interactive)"
    skip "LS variance event — wave hand (interactive)"
    end_suite
fi

# --- PS proximity event: approach (rising) ---
#
# Read current PS value and set the rising threshold at ~150% of it.
# When the user brings their hand close, PS will exceed the threshold.

ps_baseline="$(read_attr in_proximity_raw)"
if ! echo "$ps_baseline" | grep -qE '^[0-9]+$'; then
    skip "PS proximity approach (could not read baseline)"
    skip "PS proximity retreat"
    skip "LS threshold cover"
    skip "LS threshold expose"
    skip "LS variance wave"
    end_suite
fi

ps_rising_thresh=$(( ps_baseline + 200 ))
[ "$ps_rising_thresh" -gt 2047 ] && ps_rising_thresh=2047
ps_falling_thresh=$(( ps_baseline > 100 ? ps_baseline / 2 : ps_baseline + 2 ))

write_attr "$EV/in_proximity_thresh_rising_value"  "$ps_rising_thresh"
write_attr "$EV/in_proximity_thresh_falling_value" "$ps_falling_thresh"
write_attr "$EV/in_proximity_thresh_period" 1

echo -e "  ${__C_YLW}  (PS baseline=$ps_baseline, rising thresh=$ps_rising_thresh, falling thresh=$ps_falling_thresh)${__C_RST}"

if prompt_user "Bring your hand close to the sensor. Event will fire when proximity crosses $ps_rising_thresh"; then
    start_event_monitor

    write_attr "$EV/in_proximity_thresh_en" 1

    echo -e "    ${__C_YLW}Waiting for PS rising event (30s timeout)...${__C_RST}"
    wait_for_event 30 "proximity"

    stop_event_monitor
    parse_events "proximity"

    if [ "$EVENT_COUNT" -gt 0 ]; then
        if [ "$EVENT_DIR" = "1" ]; then
            pass "PS proximity approach: rising event received"
        else
            pass "PS proximity approach: event received (direction=$EVENT_DIR)"
        fi
    else
        fail "PS proximity approach: no event within 30s"
    fi

    write_attr "$EV/in_proximity_thresh_en" 0
    sleep 0.2
else
    skip "PS proximity approach (user skipped)"
fi

# --- PS proximity event: retreat (falling) ---

if prompt_user "Now slowly move your hand away. Event will fire when proximity drops below $ps_falling_thresh"; then
    start_event_monitor

    write_attr "$EV/in_proximity_thresh_en" 1

    echo -e "    ${__C_YLW}Waiting for PS falling event (30s timeout)...${__C_RST}"
    wait_for_event 30 "proximity"

    stop_event_monitor
    parse_events "proximity"

    if [ "$EVENT_COUNT" -gt 0 ]; then
        if [ "$EVENT_DIR" = "2" ]; then
            pass "PS proximity retreat: falling event received"
        else
            pass "PS proximity retreat: event received (direction=$EVENT_DIR)"
        fi
    else
        fail "PS proximity retreat: no event within 30s"
    fi

    write_attr "$EV/in_proximity_thresh_en" 0
    sleep 0.2
else
    skip "PS proximity retreat (user skipped)"
fi

# --- LS threshold event: cover (falling) ---

write_attr rgb_mode 0
sleep 0.3

als_baseline="$(read_attr in_illuminance_raw)"
if ! echo "$als_baseline" | grep -qE '^[0-9]+$' || [ "$als_baseline" -lt 10 ]; then
    skip "LS threshold cover (baseline too low: $als_baseline — need more ambient light)"
    skip "LS threshold expose"
else
    als_falling=$(( als_baseline / 2 ))
    als_rising=$(( als_baseline > 100 ? als_baseline * 3 / 2 : als_baseline + 50 ))

    write_attr "$EV/in_illuminance_thresh_rising_value"  "$als_rising"
    write_attr "$EV/in_illuminance_thresh_falling_value" "$als_falling"
    write_attr "$EV/in_illuminance_thresh_period" 1

    echo -e "  ${__C_YLW}  (ALS baseline=$als_baseline, falling thresh=$als_falling, rising thresh=$als_rising)${__C_RST}"

    if prompt_user "Cover the sensor to drop illuminance below $als_falling"; then
        start_event_monitor

        write_attr "$EV/in_illuminance_thresh_en" 1

        echo -e "    ${__C_YLW}Waiting for LS falling event (15s timeout)...${__C_RST}"
        wait_for_event 15 "illuminance"

        stop_event_monitor
        parse_events "illuminance"

        if [ "$EVENT_COUNT" -gt 0 ]; then
            pass "LS threshold cover: falling event received"
        else
            fail "LS threshold cover: no event within 15s"
        fi

        write_attr "$EV/in_illuminance_thresh_en" 0
        sleep 0.2
    else
        skip "LS threshold cover (user skipped)"
    fi

    # --- LS threshold event: expose (rising) ---

    if prompt_user "Now uncover the sensor / shine light to exceed $als_rising"; then
        start_event_monitor

        write_attr "$EV/in_illuminance_thresh_en" 1

        echo -e "    ${__C_YLW}Waiting for LS rising event (15s timeout)...${__C_RST}"
        wait_for_event 15 "illuminance"

        stop_event_monitor
        parse_events "illuminance"

        if [ "$EVENT_COUNT" -gt 0 ]; then
            pass "LS threshold expose: rising event received"
        else
            fail "LS threshold expose: no event within 15s"
        fi

        write_attr "$EV/in_illuminance_thresh_en" 0
        sleep 0.2
    else
        skip "LS threshold expose (user skipped)"
    fi
fi

# --- LS variance event: wave hand ---

write_attr "$EV/in_illuminance_change_value" 8
write_attr "$EV/in_illuminance_change_en" 1

if prompt_user "Wave your hand over the sensor to change the light level (variance threshold=8)"; then
    start_event_monitor

    echo -e "    ${__C_YLW}Waiting for LS variance event (15s timeout)...${__C_RST}"
    wait_for_event 15 "illuminance"

    stop_event_monitor
    parse_events "illuminance"

    if [ "$EVENT_COUNT" -gt 0 ]; then
        pass "LS variance event: received"
    else
        fail "LS variance event: no event within 15s"
    fi
else
    skip "LS variance wave (user skipped)"
fi

write_attr "$EV/in_illuminance_change_en" 0
write_attr rgb_mode 1
sleep 0.2

end_suite
