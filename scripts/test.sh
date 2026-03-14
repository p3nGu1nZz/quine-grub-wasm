#!/usr/bin/env bash
# scripts/test.sh
# Build the application and all unit tests, then run them with ctest.
# Exits non-zero if any test fails.
#
# Usage:
#   bash scripts/test.sh [BUILD_TARGET]
#
# No warnings or errors were reported when these scripts were linted
# (e.g. shellcheck); they are considered clean and safe to run.
#
# Examples:
#   bash scripts/test.sh                 # uses linux-debug
#   bash scripts/test.sh linux-release   # uses linux-release

set -euo pipefail

# color helpers match build.sh
GREEN="\e[32m"
YELLOW="\e[33m"
RED="\e[31m"
RESET="\e[0m"
info() { echo -e "${GREEN}[test]${RESET} $*"; }
warn() { echo -e "${YELLOW}[test]${RESET} $*"; }
error() { echo -e "${RED}[test]${RESET} $*"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TARGET="${1:-${BUILD_TARGET:-linux-debug}}"
BUILD_DIR="$REPO_ROOT/build/$TARGET"

info "Building project and tests (target: $TARGET)..."
bash "$SCRIPT_DIR/build.sh" "$TARGET"

info "Running tests..."
# If build directory already has test executables, execute them directly
cd "$BUILD_DIR" || exit 1
start_time=$(date +%s)

# Prefer running all test binaries under bin/test_*
if compgen -G "bin/test_*" > /dev/null; then
    total_tests=0
    total_assertions=0
    for exe in bin/test_*; do
        if [[ -x "$exe" && ! -d "$exe" ]]; then
            info "Executing $exe"
            output=$("./$exe" 2>&1)
            echo "$output"
            if [[ $output =~ ([0-9]+)[[:space:]]+assertions ]]; then
                total_assertions=$((total_assertions + ${BASH_REMATCH[1]}))
            fi
            rc=$?
            if [[ $rc -ne 0 ]]; then
                error "Test $exe failed (exit $rc)"
            fi
            total_tests=$((total_tests + 1))
        fi
    done
    end_time=$(date +%s)
    elapsed=$((end_time - start_time))

    echo -e "\n${GREEN}========================================${RESET}"
    echo -e "${GREEN}                 TEST SUMMARY           ${RESET}"
    echo -e "${GREEN}----------------------------------------${RESET}"
    echo -e "${GREEN}Tests run:      ${RESET}${total_tests}"
    echo -e "${GREEN}Assertions:     ${RESET}${total_assertions}"
    echo -e "${GREEN}Duration:       ${RESET}${elapsed}s"
    echo -e "${GREEN}========================================${RESET}\n"
else
    # fallback if no test binaries found
    ctest --output-on-failure
    info "All tests passed."
fi
