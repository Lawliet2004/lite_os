#!/usr/bin/env sh
# LiteNix CI-style boot verification wrapper.
#
# Runs the full boot-verification matrix (positive + negative) on Unix-like
# hosts. Use this in CI after `make` has already succeeded. Exits non-zero
# if any sub-verification fails.
#
# On Windows + MSYS2, prefer scripts/ci-boot.ps1 to avoid the long PATH
# quoting. The PowerShell script calls the same Make targets.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

echo "[ci-boot] starting boot verification matrix at $(date -u +%Y-%m-%dT%H:%M:%SZ)"

# Run the full matrix. `make verify-boot-all` rebuilds between modes because
# each negative mode uses a different TEST= setting, and `make verify-boot`
# asserts the success markers for the default (TEST=none) build.
make verify-boot-all
RC=$?

if [ $RC -ne 0 ]; then
    echo "[ci-boot] FAILED with exit code $RC" >&2
    exit $RC
fi

echo "[ci-boot] restoring default build"
make clean all

echo "[ci-boot] all boot verifications passed at $(date -u +%Y-%m-%dT%H:%M:%SZ)"
exit 0
