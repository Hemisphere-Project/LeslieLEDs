#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_PATH="$SCRIPT_DIR/LeslieLEDs.app"
APP_EXEC="$APP_PATH/Contents/MacOS/leslieleds-launcher"
DEBUG_LAUNCHER="$SCRIPT_DIR/../LeslieLEDs-Debug.command"

if [ ! -d "$APP_PATH" ]; then
    echo "LeslieLEDs.app not found at: $APP_PATH" >&2
    exit 1
fi

if [ ! -f "$APP_EXEC" ]; then
    echo "App executable not found at: $APP_EXEC" >&2
    exit 1
fi

chmod 755 "$APP_EXEC"
chmod 755 "$DEBUG_LAUNCHER" 2>/dev/null || true

if command -v xattr >/dev/null 2>&1; then
    xattr -dr com.apple.quarantine "$APP_PATH" 2>/dev/null || true
    xattr -dr com.apple.quarantine "$DEBUG_LAUNCHER" 2>/dev/null || true
fi

if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "$APP_PATH" >/dev/null 2>&1 || true
fi

touch "$APP_PATH"

echo "Normalized LeslieLEDs.app permissions."
echo "App:   $APP_PATH"
echo "Debug: $DEBUG_LAUNCHER"
echo
echo "If Finder still shows the old state, remove and re-add the app in Dock/Finder, then try again."