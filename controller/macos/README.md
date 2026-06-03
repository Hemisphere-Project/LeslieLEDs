# macOS launchers

This folder contains the no-Terminal launcher for the controller GUI:

- `LeslieLEDs.app` launches the controller hidden from Finder or the Dock.
- `../LeslieLEDs-Debug.command` launches the same bootstrap path in Terminal for debug output.

## Expected behavior

- Every launch goes through `controller/launcher.py`.
- On launch, the bootstrap tries `git pull --ff-only` when `origin` is reachable.
- Auto-update is skipped if tracked files in the checkout are already modified.
- If the GUI is already running, a second launch does not keep a second copy alive; it asks the running GUI to come forward and exits.

## How to use it on a Mac

1. Keep `LeslieLEDs.app` inside `controller/macos/` in this repository checkout.
2. Add that app to the Dock or make a Finder alias to it. Do not move the app bundle out of the repo, because it resolves `controller/launcher.py` by relative path.
3. Use `controller/LeslieLEDs-Debug.command` whenever you want startup logs and stdout/stderr in Terminal.

## Notes

- Finder launches often have a minimal `PATH`, so the wrapper adds the common Homebrew paths (`/opt/homebrew/bin` and `/usr/local/bin`) before looking for Python, `git`, or `uv`.
- The hidden launcher writes detached-run logs to `~/Library/Logs/LeslieLEDs/controller.log`.
- The focus-on-relaunch path uses `System Events` on macOS. On first use, macOS may ask for permission before allowing the running window to come to the front.
- The app icon is generated from `controller/macos/generate_icon.py`; rerun it if you want to tweak the pixel art or regenerate `LeslieLEDs.icns`.