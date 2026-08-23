#!/bin/bash
# =============================================================================
# lib.sh — Shared test library for APDS-9999 kernel driver tests
# =============================================================================
#
# Shared between the static and dynamic test suites.
#
# Design choices:
#   - No `set -e`: tests intentionally trigger sysfs write failures.
#   - `set -u` only: catches typos on unset variables.
#   - Counters use `$(( x + 1 ))` instead of `(( x++ ))` because the latter
#     returns exit code 1 when x was 0, causing subtle bugs.
#   - All sysfs I/O goes through read_attr / write_attr for consistent
#     error handling and path resolution.
#   - The results-file protocol lets the runner aggregate counts across
#     child processes without fragile stdout parsing.
# =============================================================================

set -u

__pass=0
__fail=0
__skip=0
IIO_DEV=""

# Color helpers — degrade when stdout is not a terminal
if [ -t 1 ]; then
    __C_GRN="\033[0;32m"
    __C_RED="\033[0;31m"
    __C_YLW="\033[0;33m"
    __C_BLD="\033[1m"
    __C_RST="\033[0m"
else
    __C_GRN="" __C_RED="" __C_YLW="" __C_BLD="" __C_RST=""
fi

# find_device() — auto-detect the APDS-9999 IIO device
find_device() {
    local dev
    for dev in /sys/bus/iio/devices/iio:device*; do
        [ -d "$dev" ] || continue
        if grep -qi "apds9999" "$dev/name" 2>/dev/null; then
            IIO_DEV="$dev"
            return 0
        fi
    done
    echo -e "${__C_RED}ERROR: No APDS-9999 IIO device found.${__C_RST}"
    exit 2
}

read_attr() { cat "$IIO_DEV/$1" 2>/dev/null; }

write_attr() { echo "$2" > "$IIO_DEV/$1" 2>/dev/null; }

pass() { __pass=$(( __pass + 1 )); echo -e "  ${__C_GRN}PASS${__C_RST}  $1"; }
fail() { __fail=$(( __fail + 1 )); echo -e "  ${__C_RED}FAIL${__C_RST}  $1"; }
skip() { __skip=$(( __skip + 1 )); echo -e "  ${__C_YLW}SKIP${__C_RST}  $1"; }

# expect_eq(desc, expected, actual) — string equality
expect_eq() {
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1 (expected '$2', got '$3')"
    fi
}

# expect_ne(desc, not_expected, actual) — string inequality
expect_ne() {
    if [ "$2" != "$3" ]; then
        pass "$1"
    else
        fail "$1 (should not be '$2', got '$3')"
    fi
}

# expect_in(desc, needle, haystack) — substring check
expect_in() {
    if echo "$3" | grep -qF -- "$2"; then
        pass "$1"
    else
        fail "$1 (expected '$3' to contain '$2')"
    fi
}

# expect_in_range(desc, min, max, actual) — inclusive integer range
expect_in_range() {
    local actual="$4"
    if ! echo "$actual" | grep -qE '^-?[0-9]+$'; then
        fail "$1 (value '$actual' is not an integer)"
        return
    fi
    if [ "$actual" -ge "$2" ] && [ "$actual" -le "$3" ]; then
        pass "$1"
    else
        fail "$1 (expected $2..$3, got $actual)"
    fi
}

# expect_numeric(desc, value) — integer check (possibly negative)
expect_numeric() {
    if echo "$2" | grep -qE '^-?[0-9]+$'; then
        pass "$1"
    else
        fail "$1 (expected integer, got '$2')"
    fi
}

# expect_positive(desc, value) — positive integer (> 0)
expect_positive() {
    if echo "$2" | grep -qE '^[0-9]+$' && [ "$2" -gt 0 ]; then
        pass "$1"
    else
        fail "$1 (expected positive integer, got '$2')"
    fi
}

# expect_nonneg(desc, value) — non-negative integer (>= 0)
expect_nonneg() {
    if echo "$2" | grep -qE '^[0-9]+$'; then
        pass "$1"
    else
        fail "$1 (expected non-negative integer, got '$2')"
    fi
}

# expect_decimal(desc, value) — decimal number check
expect_decimal() {
    if echo "$2" | grep -qE '^-?[0-9]+\.?[0-9]*$'; then
        pass "$1"
    else
        fail "$1 (expected decimal number, got '$2')"
    fi
}

# expect_write_fail(desc, attr, value) — expects the sysfs write to fail
expect_write_fail() {
    if [ ! -e "$IIO_DEV/$2" ]; then
        fail "$1 (attribute '$2' does not exist)"
        return
    fi
    if write_attr "$2" "$3" 2>/dev/null; then
        fail "$1 (write of '$3' should have failed but succeeded)"
    else
        pass "$1"
    fi
}

# test_write_read(desc, attr, value) — write then read back
test_write_read() {
    if ! write_attr "$2" "$3"; then
        fail "$1 (write of '$3' to '$2' failed)"
        return
    fi
    local actual
    actual="$(read_attr "$2")"
    expect_eq "$1" "$3" "$actual"
}

# reset_sensor() — restore known baseline state
reset_sensor() {
    write_attr rgb_mode        1     2>/dev/null || true
    write_attr ps_enable       1     2>/dev/null || true
    write_attr ls_enable       1     2>/dev/null || true
    write_attr ps_reso_bit     11    2>/dev/null || true
    write_attr in_proximity_calibbias 0 2>/dev/null || true
    write_attr ps_analog_cancellation 0 2>/dev/null || true
    write_attr ls_reso_bit     16    2>/dev/null || true
    write_attr ls_meas_rate_ms 100   2>/dev/null || true
    write_attr ps_meas_rate_us 25000 2>/dev/null || true
}

begin_suite() {
    echo -e "${__C_BLD}── $1 ──${__C_RST}"
}

end_suite() {
    echo ""
    echo -e "Results: ${__C_GRN}${__pass} passed${__C_RST}, ${__C_RED}${__fail} failed${__C_RST}, ${__C_YLW}${__skip} skipped${__C_RST}"

    if [ -n "${APDS9999_RESULTS_FILE:-}" ]; then
        echo "$__pass $__fail $__skip" >> "$APDS9999_RESULTS_FILE"
    fi

    [ "$__fail" -gt 0 ] && exit 1
    exit 0
}
