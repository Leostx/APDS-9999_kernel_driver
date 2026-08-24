#!/bin/bash
# =============================================================================
# dynamic/lib.sh — Extended test library for dynamic APDS-9999 tests
# =============================================================================
#
# Sources the shared library from tests/lib.sh and adds helpers for:
#   - Character device access (event reading, buffer capture)
#   - Interactive user prompts
#   - Trigger and buffer management
#   - ASCII sparkline visualization
# =============================================================================

# Resolve path to the shared library
_DYN_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$_DYN_LIB_DIR/../lib.sh"

# =================== KERNEL TOOLS ===================

# Path to the precompiled IIO userspace tools (iio_event_monitor, iio_generic_buffer)
_KERNEL_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../kernel-tools" 2>/dev/null && pwd)"

# =================== IIO CHARACTER DEVICE ===================

IIO_CHARDEV=""

# find_chardev() — resolve /dev/iio:deviceN from the IIO sysfs path
find_chardev() {
    local devname
    devname="$(basename "$IIO_DEV")"
    IIO_CHARDEV="/dev/$devname"
    if [ ! -c "$IIO_CHARDEV" ]; then
        echo -e "${__C_RED}ERROR: Character device $IIO_CHARDEV not found.${__C_RST}"
        exit 2
    fi
}

# =================== EVENT MONITORING (via iio_event_monitor) ===================

# _EMON_PID / _EMON_OUT — internal state for event capture
EMON_PID=""
EMON_OUT=""

# start_event_monitor() — launch iio_event_monitor in the background
#
# Uses the IIO device name to find the correct chardev.
# Call stop_event_monitor to collect output and kill the process.
start_event_monitor() {
    local devname
    #devname="$(basename "$IIO_DEV")"
    devname="apds9999"

    EMON_OUT="$(mktemp)"
    "$_KERNEL_TOOLS_DIR/iio_event_monitor" "$devname" > "$EMON_OUT" &

    EMON_PID=$!
    # Give the tool time to open the chardev and start polling
    sleep 0.3
}

# stop_event_monitor() — kill the monitor and clean up
#
# Sets EMON_PID to empty.  Caller should inspect EMON_OUT before calling this.
stop_event_monitor() {
    if [ -n "$EMON_PID" ]; then
        kill "$EMON_PID" 2>/dev/null || true
        wait "$EMON_PID" 2>/dev/null || true
        EMON_PID=""
    fi
}

# parse_events(filter_keyword) — parse iio_event_monitor output
#
# Reads EMON_OUT, counts lines matching "Event:" and extracts fields.
# filter_keyword: optional substring to match (e.g. "proximity", "illuminance").
#                 Use "" or "*" for any event.
#
# Sets: EVENT_COUNT, EVENT_TS, EVENT_DIR, EVENT_TYPE
parse_events() {
    local filter="${1:-}"
    EVENT_COUNT=0
    EVENT_TS=""
    EVENT_DIR=""
    EVENT_TYPE=""

    [ -z "$EMON_OUT" ] || [ ! -f "$EMON_OUT" ] && return

    while IFS= read -r line; do
        [[ "$line" == Event:* ]] || continue

        # Optional keyword filter
        if [ -n "$filter" ] && [ "$filter" != "*" ]; then
            echo "$line" | grep -qi "$filter" || continue
        fi

        EVENT_COUNT=$(( EVENT_COUNT + 1 ))

        # Extract fields from: Event: time: <ts>, type: <type>, evtype: <evtype>, direction: <dir>
        EVENT_TS="$(echo "$line" | sed -n 's/.*time: \([0-9]*\).*/\1/p')"
        EVENT_TYPE="$(echo "$line" | sed -n 's/.*direction: \([a-z]*\).*/\1/p')"

        # Direction text → numeric: rising=1, falling=2, either=3
        case "$EVENT_TYPE" in
            rising)  EVENT_DIR=1 ;;
            falling) EVENT_DIR=2 ;;
            either)  EVENT_DIR=3 ;;
            *)       EVENT_DIR=0 ;;
        esac
    done < "$EMON_OUT"

    # Clean up the temp file after parsing
    [ -n "$EMON_OUT" ] && rm -f "$EMON_OUT"
    EMON_OUT=""
}

# wait_for_event(timeout_sec, filter_keyword) — poll EMON_OUT for an event
#
# Watches the iio_event_monitor output file for a line matching "Event:"
# (optionally filtered by filter_keyword).  Returns 0 as soon as a match
# appears, or 1 if the timeout expires.
#
# Must be called after start_event_monitor and before stop_event_monitor.
wait_for_event() {
    local timeout_sec="${1:-30}"
    local filter="${2:-}"

    if [ -z "$EMON_OUT" ] || [ ! -f "$EMON_OUT" ]; then
        return 1
    fi

    local deadline=$(( $(date +%s) + timeout_sec ))

    while [ "$(date +%s)" -lt "$deadline" ]; do
        if [ -n "$filter" ]; then
            grep -qi "Event:.*${filter}" "$EMON_OUT" 2>/dev/null && return 0
        else
            grep -q "Event:" "$EMON_OUT" 2>/dev/null && return 0
        fi
        sleep 0.2
    done
    return 1
}

# event_direction() — return last parsed direction (numeric)
event_direction() {
    echo "${EVENT_DIR:-0}"
}

# event_channel_type() — placeholder for compat (unused with monitor approach)
event_channel_type() {
    echo 0
}

# =================== TRIGGER MANAGEMENT (sysfs) ===================

# TRIG_SYSFS_PATH — path to the sysfs trigger directory (e.g. /sys/bus/iio/devices/trigger0)
TRIG_SYSFS_PATH=""
# TRIG_SYSFS_NAME — trigger name as seen by the kernel (e.g. sysfs-trigger-0)
TRIG_SYSFS_NAME=""

# _find_sysfs_trigger() — locate the sysfs directory of the first sysfs-type trigger
#
# Prints the trigger path (e.g. /sys/bus/iio/devices/trigger0) on success.
_find_sysfs_trigger() {
    local trig name
    for trig in /sys/bus/iio/devices/trigger*; do
        [ -d "$trig" ] || continue
        name="$(cat "$trig/name" 2>/dev/null)"
        if echo "$name" | grep -q "sysfs"; then
            echo "$trig"
            return 0
        fi
    done
    return 1
}

# ensure_sysfs_trigger() — create a sysfs trigger and assign it to our device
#
# 1. Loads iio-trig-sysfs module
# 2. Creates a sysfs trigger via the iio_sysfs_trigger sysfs interface
# 3. Assigns the trigger to the IIO device
#
# On success, sets TRIG_SYSFS_PATH and TRIG_SYSFS_NAME.
# Returns 0 on success, 1 if the trigger cannot be created or assigned.
ensure_sysfs_trigger() {
    modprobe iio-trig-sysfs 2>/dev/null || true

    # If no sysfs trigger exists yet, create one
    if ! _find_sysfs_trigger >/dev/null 2>&1; then
        echo 1 > /sys/bus/iio/devices/iio_sysfs_trigger/add_trigger 2>/dev/null || true
    fi

    TRIG_SYSFS_PATH="$(_find_sysfs_trigger)"
    if [ -z "$TRIG_SYSFS_PATH" ]; then
        echo "  ensure_sysfs_trigger: no sysfs trigger found after modprobe + add_trigger" >&2
        return 1
    fi

    TRIG_SYSFS_NAME="$(cat "$TRIG_SYSFS_PATH/name" 2>/dev/null)"

    # Assign this trigger to our IIO device
    write_attr trigger/current_trigger "$TRIG_SYSFS_NAME"
    return 0
}

# fire_sysfs_trigger() — trigger a single sample conversion
#
# Writes 1 to the sysfs trigger's trigger_now attribute.
fire_sysfs_trigger() {
    echo 1 > "$TRIG_SYSFS_PATH/trigger_now" 2>/dev/null
}

# =================== BUFFER HELPERS ===================

# buffer_available() — check if iio_generic_buffer is available in kernel-tools/
buffer_available() {
    [ -x "$_KERNEL_TOOLS_DIR/iio_generic_buffer" ]
}

# disable_all_scan_elements() — disable every scan element
disable_all_scan_elements() {
    for ch in in_proximity_en in_intensity_red_en in_intensity_green_en \
              in_intensity_blue_en in_intensity_ir_en in_illuminance_en; do
        write_attr "scan_elements/$ch" 0 2>/dev/null || true
    done
}

# ensure_buffer_off() — disable the buffer
ensure_buffer_off() {
    write_attr buffer/enable 0 2>/dev/null || true
}

# capture_buffer_samples(num_samples, _unused, output_file)
#
# Uses iio_generic_buffer to capture N samples via the sysfs trigger.
# The scan elements and buffer must be already enabled via sysfs before calling.
# The second argument (sleep_between_ms) is accepted for API compat but ignored.
# Each sample is produced by firing the sysfs trigger once.
# The tool output (stdout) is saved to output_file.
capture_buffer_samples() {
    local num_samples="$1"
    local output_file="$3"
    local devname
    devname="apds9999"

    # Start iio_generic_buffer in the background with the sysfs trigger.
    # -n <device>  — IIO device name
    # -t <trigger> — trigger name (sysfs trigger)
    # -c <count>   — number of samples to capture
    "$_KERNEL_TOOLS_DIR/iio_generic_buffer" \
        -n "$devname" -t "$TRIG_SYSFS_NAME" -c "$num_samples" \
        > "$output_file" &
    local buf_pid=$!

    #echo "Running: $_KERNEL_TOOLS_DIR/iio_generic_buffer -n $devname -t $TRIG_SYSFS_NAME -c $num_samples > $output_file"


    # Give the tool time to open the chardev and arm the buffer
    sleep 0.3

    # Fire the sysfs trigger once per sample
    local i
    for (( i = 0; i < num_samples; i++ )); do
        fire_sysfs_trigger
        sleep 0.05
    done

    # Wait for iio_generic_buffer to finish
    wait "$buf_pid"
    return $?
}

# count_buffer_lines(file) — count non-empty, non-header lines in buffer output
count_buffer_lines() {
    grep -cE '^[0-9 ]' "$1" 2>/dev/null || echo 0
}

# =================== INTERACTIVE HELPERS ===================

# interactive_enabled — returns 0 if interactive mode is on
#
# Enabled when stdin is a TTY and APDS9999_INTERACTIVE is not "0".
interactive_enabled() {
    [ -t 0 ] && [ "${APDS9999_INTERACTIVE:-1}" != "0" ]
}

# prompt_user(message)
#
# Prints the message and blocks until the user presses ENTER.
# Returns 0 normally.  Returns 1 if the user types 's' (skip).
prompt_user() {
    echo -e "  ${__C_YLW}▶ $1${__C_RST}"
    local reply=""
    read -r -p "    Press ENTER when ready (or 's' to skip)... " reply </dev/tty
    if [ "$reply" = "s" ] || [ "$reply" = "S" ]; then
        return 1
    fi
    return 0
}

# prompt_yn(message, timeout_sec)
#
# Asks a yes/no question.  Returns 0 for yes, 1 for no/timeout.
prompt_yn() {
    local msg="$1"
    local timeout_sec="${2:-30}"
    echo -e "  ${__C_YLW}▶ $msg${__C_RST}"
    local reply=""
    read -r -t "$timeout_sec" -p "    (y/n): " reply </dev/tty
    [ "$reply" = "y" ] || [ "$reply" = "Y" ]
}

# print_readings(label, ps, red, green, blue, ir, als)
#
# Prints sensor readings in a compact format.  Empty strings are omitted.
print_readings() {
    local label="$1"; shift
    local ps="${1:-}"; shift
    local red="${1:-}"; shift
    local green="${1:-}"; shift
    local blue="${1:-}"; shift
    local ir="${1:-}"; shift
    local als="${1:-}"

    local parts=""
    [ -n "$ps" ]    && parts="${parts}PS=$ps  "
    [ -n "$red" ]   && parts="${parts}R=$red  "
    [ -n "$green" ] && parts="${parts}G=$green  "
    [ -n "$blue" ]  && parts="${parts}B=$blue  "
    [ -n "$ir" ]    && parts="${parts}IR=$ir  "
    [ -n "$als" ]   && parts="${parts}ALS=$als  "

    echo -e "    ${__C_BLD}$label:${__C_RST}  $parts"
}

# sparkline(min, max, val1, val2, ...)
#
# Prints an ASCII sparkline using Unicode block characters.
# Maps each value to ▁▂▃▄▅▆▇█ based on the min–max range.
sparkline() {
    # Truncate to integers — callers may pass floats from iio_generic_buffer output
    local min; min=$(printf '%.0f' "$1"); shift
    local max; max=$(printf '%.0f' "$1"); shift
    local vals=()
    for v in "$@"; do
        vals+=( "$(printf '%.0f' "$v")" )
    done
    local range=$(( max - min ))

    if [ "$range" -le 0 ]; then
        echo "────────────────────"
        return
    fi

    local spark=""
    local blocks="▁▂▃▄▅▆▇█"
    for val in "${vals[@]}"; do
        local idx=$(( (val - min) * 7 / range ))
        [ "$idx" -gt 7 ] && idx=7
        [ "$idx" -lt 0 ] && idx=0
        spark="${spark}${blocks:$idx:1}"
    done
    echo "$spark"
}

# cleanup_dynamic() — disable events, buffer, scan elements
#
# Called from trap EXIT in test scripts.  Tolerates missing attributes.
cleanup_dynamic() {
    # Disable all events
    local ev="events"
    write_attr "$ev/in_proximity_thresh_en"    0 2>/dev/null || true
    write_attr "$ev/in_illuminance_thresh_en"  0 2>/dev/null || true
    write_attr "$ev/in_illuminance_change_en"  0 2>/dev/null || true

    # Buffer cleanup
    ensure_buffer_off 2>/dev/null || true
    disable_all_scan_elements 2>/dev/null || true

    # Reset sensor to known state
    reset_sensor
}
