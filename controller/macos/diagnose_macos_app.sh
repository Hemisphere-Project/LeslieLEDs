#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_PATH="$SCRIPT_DIR/LeslieLEDs.app"
APP_EXEC="$APP_PATH/Contents/MacOS/leslieleds-launcher"
DEBUG_LAUNCHER="$SCRIPT_DIR/../LeslieLEDs-Debug.command"

echo "[diag] app bundle: $APP_PATH"
echo "[diag] app exec:   $APP_EXEC"
echo "[diag] debug cmd:  $DEBUG_LAUNCHER"
echo

if [ ! -d "$APP_PATH" ]; then
    echo "[diag] ERROR: app bundle not found" >&2
    exit 1
fi

if [ ! -f "$APP_EXEC" ]; then
    echo "[diag] ERROR: app executable not found" >&2
    exit 1
fi

echo "[diag] bundle permissions:"
ls -ld "$APP_PATH"
echo
echo "[diag] executable permissions:"
ls -l "$APP_EXEC"
echo

if command -v xattr >/dev/null 2>&1; then
    echo "[diag] quarantine attributes:"
    xattr -lr "$APP_PATH" 2>/dev/null || true
    echo
fi

echo "[diag] normalizing app permissions first..."
"$SCRIPT_DIR/install_macos_app.sh"
echo

echo "[diag] correct launch methods:"
echo "  Finder: double-click LeslieLEDs.app"
echo "  Terminal app launch: open \"$APP_PATH\""
echo "  Direct inner launcher: \"$APP_EXEC\""
echo "  Debug terminal path: \"$DEBUG_LAUNCHER\""
echo

if [ "${1:-}" = "--open" ]; then
    echo "[diag] launching via: open \"$APP_PATH\""
    open "$APP_PATH"
elif [ "${1:-}" = "--direct" ]; then
    echo "[diag] launching direct inner executable"
    "$APP_EXEC"
else
    echo "[diag] no launch performed. Pass --open or --direct to test from this script."
fi