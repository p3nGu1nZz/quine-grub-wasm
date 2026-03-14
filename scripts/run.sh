#!/usr/bin/env bash
# scripts/run.sh
# Build (if needed) then run the bootloader.
# All arguments are forwarded to the bootloader executable.
#
# Usage:
#   bash scripts/run.sh              # terminal/headless mode (default)
#   bash scripts/run.sh --gui        # SDL3 GUI window mode
#   bash scripts/run.sh --gui --verbose

set -euo pipefail

# colour helpers
GREEN="\e[32m"
YELLOW="\e[33m"
RED="\e[31m"
RESET="\e[0m"
info() { echo -e "${GREEN}[run]${RESET} $*"; }
warn() { echo -e "${YELLOW}[run]${RESET} $*"; }
error() { echo -e "${RED}[run]${RESET} $*"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TARGET="${BUILD_TARGET:-linux-debug}"
BINARY="$REPO_ROOT/build/$TARGET/bin/bootloader"

# Build the project automatically if the binary is missing
if [[ ! -f "$BINARY" ]]; then
    info "Binary not found – building ($TARGET) first..."
    bash "$SCRIPT_DIR/build.sh" "$TARGET"
fi
# after building the binary may now live under bin/; update path
BINARY="$REPO_ROOT/build/$TARGET/bin/bootloader"

# if no args provided default to GUI mode (was previously headless by default)
if [[ $# -eq 0 ]]; then
    set -- "--gui"
fi

# change into the build directory so logs/seq folders are created there
BUILD_DIR="$REPO_ROOT/build/$TARGET"
cd "$BUILD_DIR"
# ensure bin directories exist and relocate binary symlink if necessary
mkdir -p bin/logs
mkdir -p bin/seq


# monitoring support: if --monitor passed, run in background and tail logs
info "Working directory: $BUILD_DIR"
info "Invoking bootloader: $BINARY $*"

# display where logs/telemetry will be written for clarity
info "Logs directory: $BUILD_DIR/bin/logs"
info "Telemetry directory: $BUILD_DIR/bin/seq"

if [[ "$1" == "--monitor" ]]; then
    shift
    ./bootloader "$@" &
    pid=$!
    # tail all existing/future log files
    tail -F bin/logs/*.log &
    tailpid=$!
    wait $pid
    kill $tailpid 2>/dev/null || true
    exit 0
fi

# run bootloader normally and report exit status
# use the precomputed BINARY path instead of assuming current dir
"$BINARY" "$@"
rc=$?
info "Bootloader exited with code $rc"

# if logs were produced, show the last few lines to give immediate feedback
if ls bin/logs/*.log >/dev/null 2>&1; then
    info "Recent log output (last 20 lines of each file):"
    for f in bin/logs/*.log; do
        echo -e "\n--- $f ---"
        tail -n 20 "$f" || true
    done
fi

exit $rc
