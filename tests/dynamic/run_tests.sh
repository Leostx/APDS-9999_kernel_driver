#!/bin/bash
# =============================================================================
# run_tests.sh — Runner for the APDS-9999 dynamic test suite
#
# Runs every test_*.sh in the same directory, collects results via a shared
# temp file, and prints a two-tier summary (suite-level + test-level).
#
# Usage:
#   sudo bash tests/dynamic/run_tests.sh              # full run (auto + interactive)
#   APDS9999_INTERACTIVE=0 sudo bash run_tests.sh     # auto-only (skip interactive)
# =============================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/../lib.sh"

if interactive_enabled 2>/dev/null || ([ -t 0 ] && [ "${APDS9999_INTERACTIVE:-1}" != "0" ]); then
    __mode="interactive + auto"
else
    __mode="auto-only"
fi

echo -e "${__C_BLD}=== APDS-9999 Dynamic Test Suite (${__mode}) ===${__C_RST}"
echo ""

find_device
echo "Device: $IIO_DEV"
echo ""

results_file="$(mktemp)"
export APDS9999_RESULTS_FILE="$results_file"
trap 'rm -f "$results_file"' EXIT

total_pass=0
total_fail=0
total_skip=0
suites_run=0
suites_pass=0
suites_fail=0

for test_file in "$SCRIPT_DIR"/test_*.sh; do
    [ -f "$test_file" ] || continue

    suites_run=$(( suites_run + 1 ))
    suite_name="$(basename "$test_file" .sh)"

    echo -e "${__C_BLD}▸ $suite_name${__C_RST}"

    bash "$test_file"
    suite_exit=$?

    s_pass=0; s_fail=0; s_skip=0
    if [ -s "$results_file" ]; then
        read -r s_pass s_fail s_skip < <(tail -n 1 "$results_file")
    fi

    if [ "$suite_exit" -eq 2 ] && [ "$s_pass" -eq 0 ] && [ "$s_fail" -eq 0 ]; then
        echo -e "  ${__C_RED}SUITE ABORTED — device not found${__C_RST}"
        s_fail=1
    fi

    total_pass=$(( total_pass + s_pass ))
    total_fail=$(( total_fail + s_fail ))
    total_skip=$(( total_skip + s_skip ))

    if [ "$s_fail" -eq 0 ]; then
        suites_pass=$(( suites_pass + 1 ))
    else
        suites_fail=$(( suites_fail + 1 ))
    fi

    : > "$results_file"
    echo ""
done

if [ "$suites_run" -eq 0 ]; then
    echo -e "${__C_YLW}No test_*.sh files found in $SCRIPT_DIR${__C_RST}"
    exit 0
fi

echo -e "${__C_BLD}══════════════════════════════${__C_RST}"
echo -e "${__C_BLD}  Final Summary${__C_RST}"
echo -e "${__C_BLD}══════════════════════════════${__C_RST}"
echo ""
echo -e "  Suites:  $suites_run total, ${__C_GRN}$suites_pass passed${__C_RST}, ${__C_RED}$suites_fail failed${__C_RST}"
echo -e "  Tests:   $(( total_pass + total_fail + total_skip )) total, ${__C_GRN}$total_pass passed${__C_RST}, ${__C_RED}$total_fail failed${__C_RST}, ${__C_YLW}$total_skip skipped${__C_RST}"
echo ""

if [ "$total_fail" -eq 0 ]; then
    echo -e "  ${__C_GRN}${__C_BLD}All tests passed.${__C_RST}"
    exit 0
else
    echo -e "  ${__C_RED}${__C_BLD}Some tests failed.${__C_RST}"
    exit 1
fi
