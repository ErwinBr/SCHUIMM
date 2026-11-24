#!/usr/bin/env bash
# Runs one CTest label for one test preset and reports exactly which tests and test cases ran.
#
#   run-tests.sh <test-preset> <label> [wrapper command...]
#
# Example: run-tests.sh linux-gcc-debug integration xvfb-run -a
#
# - Fails if no test carries the label (the presets set noTestsAction=error).
# - Writes a JUnit report to Build/test-results/<preset>-<label>.xml.
# - Appends a summary (CTest tests and the individual SCHUIMM_TEST cases) to
#   $GITHUB_STEP_SUMMARY when it is set, so the run page shows what was executed.
# - Exits with CTest's exit code: any failing test fails the CI step.
set -uo pipefail

preset="$1"
label="$2"
shift 2

results_dir="$PWD/Build/test-results"
mkdir -p "$results_dir"
junit="$results_dir/${preset}-${label}.xml"
log="$results_dir/${preset}-${label}.log"

echo "::group::Tests labelled '$label' in preset '$preset'"
ctest --preset "$preset" -L "$label" -N
echo "::endgroup::"

# --verbose echoes each test executable's output, i.e. one "[ RUN  ]"/"[  OK  ]" line per case.
"$@" ctest --preset "$preset" -L "$label" --output-on-failure --verbose --output-junit "$junit" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}

if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    {
        echo "### \`$label\` tests: \`$preset\`"
        echo
        echo '```text'
        grep -E '^ *[0-9]+/[0-9]+ Test +#' "$log" | sed -E 's/^ *//' || true
        grep -E 'tests passed|tests failed' "$log" || true
        echo '```'
        echo
        echo "<details><summary>Test cases</summary>"
        echo
        echo '```text'
        grep -E '^[0-9]+: \[ +(OK|FAIL) +\]' "$log" | sed -E 's/^[0-9]+: //' || true
        echo '```'
        echo "</details>"
        echo
    } >> "$GITHUB_STEP_SUMMARY"
fi

exit "$status"
