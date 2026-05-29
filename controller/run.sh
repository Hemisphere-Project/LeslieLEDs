#!/bin/bash
# Run the LeslieLEDs controller.
# Pass --headless (and optionally --port <substring>) to run without GUI.
set -e

cd "$(dirname "$0")"

# Create venv if it doesn't exist
if [ ! -d ".venv" ]; then
    echo "Creating virtual environment with uv..."
    uv venv
fi

# Pick deps based on mode. Headless skips DearPyGUI so the bridge can
# run on a headless server with no display libraries.
HEADLESS=0
for arg in "$@"; do
    if [ "$arg" = "--headless" ]; then HEADLESS=1; fi
done

if [ "$HEADLESS" -eq 1 ]; then
    echo "Installing dependencies (headless)..."
    uv pip install python-rtmidi pyserial
else
    echo "Installing dependencies..."
    uv pip install dearpygui python-rtmidi pyserial
fi

echo "Starting LeslieLEDs controller..."
exec .venv/bin/python controller.py "$@"
