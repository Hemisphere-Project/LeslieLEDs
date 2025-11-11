#!/bin/bash
# Run the LeslieLEDs controller

cd "$(dirname "$0")"

# Create venv if it doesn't exist
if [ ! -d ".venv" ]; then
    echo "Creating virtual environment with uv..."
    uv venv
fi

# Install dependencies directly
echo "Installing dependencies..."
uv pip install dearpygui python-rtmidi

# Run the controller
echo "Starting LeslieLEDs controller..."
.venv/bin/python controller.py
