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

## If macOS says "Permission denied"

Run this once in Terminal from the repo checkout:

```bash
cd controller/macos
./install_macos_app.sh
```

That script reapplies the executable bit to the app launcher, clears the common quarantine attribute from the app bundle, and refreshes Finder's view of the bundle.

Do not drag `LeslieLEDs.app` itself into Terminal and hit Return. A `.app` is a directory bundle, so the shell will report `permission denied` if you try to execute the bundle path directly.

Use one of these instead:

```bash
open "controller/macos/LeslieLEDs.app"
"controller/macos/LeslieLEDs.app/Contents/MacOS/leslieleds-launcher"
"controller/LeslieLEDs-Debug.command"
```

If you want a one-shot diagnosis on the Mac:

```bash
cd controller/macos
./diagnose_macos_app.sh
./diagnose_macos_app.sh --open
```

## Notes

- Finder launches often have a minimal `PATH`, so the wrapper adds the common Homebrew paths (`/opt/homebrew/bin` and `/usr/local/bin`) before looking for Python, `git`, or `uv`.
- The hidden launcher writes detached-run logs to `~/Library/Logs/LeslieLEDs/controller.log`.
- The focus-on-relaunch path uses `System Events` on macOS. On first use, macOS may ask for permission before allowing the running window to come to the front.
- The app icon is generated from `controller/macos/generate_icon.py`; rerun it if you want to tweak the pixel art or regenerate `LeslieLEDs.icns`.