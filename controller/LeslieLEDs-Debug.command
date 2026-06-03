#!/bin/bash
# Finder-friendly debug launcher: opens in Terminal and keeps output visible.
set -u

cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN=python3
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN=python
else
    echo "python3 or python not found in PATH" >&2
    exit 1
fi

"$PYTHON_BIN" launcher.py "$@"
exit_code=$?

if [ -z "${LESLIELEDS_NO_PAUSE:-}" ]; then
    printf "\nPress Return to close..."
    read -r _
fi

exit "$exit_code"