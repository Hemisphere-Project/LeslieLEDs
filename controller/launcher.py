#!/usr/bin/env python3
"""Bootstrap launcher for the LeslieLEDs controller.

Keeps the controller running from the live git checkout so launch-time
fast-forward updates still apply, while also supporting a detached launch
mode for a Finder-friendly macOS app wrapper.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path


CONTROLLER_DIR = Path(__file__).resolve().parent
REPO_ROOT = CONTROLLER_DIR.parent
VENV_DIR = CONTROLLER_DIR / ".venv"
PYPROJECT_FILE = CONTROLLER_DIR / "pyproject.toml"
REQUIREMENTS_FILE = CONTROLLER_DIR / "requirements.txt"
LOCK_FILE = CONTROLLER_DIR / "uv.lock"
STAMP_FILE = VENV_DIR / ".deps-stamp"
COMMON_MAC_PATH_PREFIXES = ["/opt/homebrew/bin", "/usr/local/bin"]
HEADLESS_PACKAGES = [
    "python-rtmidi>=1.5.0",
    "pyserial>=3.5",
]


def _log(message: str):
    print(f"[launcher] {message}", flush=True)


def _prepend_common_mac_paths():
    current = os.environ.get("PATH", "")
    parts = [path for path in COMMON_MAC_PATH_PREFIXES if path]
    if current:
        parts.append(current)
    os.environ["PATH"] = os.pathsep.join(dict.fromkeys(parts))


def _venv_python() -> Path:
    if os.name == "nt":
        return VENV_DIR / "Scripts" / "python.exe"
    return VENV_DIR / "bin" / "python"


def _default_log_path() -> Path:
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Logs" / "LeslieLEDs" / "controller.log"
    return Path.home() / ".local" / "state" / "leslieleds" / "controller.log"


def _run(
    command: list[str],
    *,
    cwd: Path | None = None,
    timeout: float | None = None,
    capture_output: bool = False,
    check: bool = False,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        text=True,
        timeout=timeout,
        capture_output=capture_output,
        check=check,
        env=env,
    )


def _git_env() -> dict[str, str]:
    env = os.environ.copy()
    env.setdefault("GIT_TERMINAL_PROMPT", "0")
    return env


def _summarize_output(*chunks: str) -> str:
    for chunk in chunks:
        for line in chunk.splitlines():
            stripped = line.strip()
            if stripped:
                return stripped
    return ""


def _is_git_checkout() -> bool:
    return (REPO_ROOT / ".git").exists() and shutil.which("git") is not None


def _tracked_checkout_is_dirty() -> bool:
    result = _run(
        ["git", "status", "--porcelain", "--untracked-files=no"],
        cwd=REPO_ROOT,
        timeout=10,
        capture_output=True,
        env=_git_env(),
    )
    if result.returncode != 0:
        _log(f"Skipping auto-update: {_summarize_output(result.stdout, result.stderr) or 'git status failed.'}")
        return True
    return bool(result.stdout.strip())


def _origin_is_reachable() -> bool:
    try:
        result = _run(
            ["git", "ls-remote", "--exit-code", "origin", "HEAD"],
            cwd=REPO_ROOT,
            timeout=4,
            capture_output=True,
            env=_git_env(),
        )
    except subprocess.TimeoutExpired:
        _log("Skipping auto-update: origin reachability check timed out.")
        return False

    if result.returncode == 0:
        return True

    summary = _summarize_output(result.stdout, result.stderr)
    if summary:
        _log(f"Skipping auto-update: {summary}")
    return False


def maybe_fast_forward_update(enabled: bool):
    if not enabled:
        _log("Auto-update disabled for this launch.")
        return
    if not _is_git_checkout():
        _log("No git checkout or git executable detected; skipping auto-update.")
        return
    if _tracked_checkout_is_dirty():
        _log("Tracked local changes detected; skipping auto-update.")
        return
    if not _origin_is_reachable():
        return

    _log("Checking for fast-forward updates...")
    try:
        result = _run(
            ["git", "pull", "--ff-only"],
            cwd=REPO_ROOT,
            timeout=30,
            capture_output=True,
            env=_git_env(),
        )
    except subprocess.TimeoutExpired:
        _log("Skipping auto-update: git pull timed out.")
        return

    summary = _summarize_output(result.stdout, result.stderr)
    if result.returncode == 0:
        _log(summary or "Repository already up to date.")
    else:
        _log(f"Auto-update failed, continuing with local checkout: {summary or 'git pull failed.'}")


def _dependency_token(headless: bool) -> str:
    digest = hashlib.sha256()
    digest.update(f"headless={int(headless)}\n".encode("utf-8"))
    if headless:
        for package in HEADLESS_PACKAGES:
            digest.update(package.encode("utf-8"))
            digest.update(b"\0")
        return digest.hexdigest()

    for path in (REQUIREMENTS_FILE, PYPROJECT_FILE, LOCK_FILE):
        digest.update(path.name.encode("utf-8"))
        digest.update(b"\0")
        if path.exists():
            digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def ensure_venv() -> Path:
    venv_python = _venv_python()
    if venv_python.exists():
        return venv_python

    _log("Creating controller virtual environment...")
    uv = shutil.which("uv")
    if uv:
        _run([uv, "venv", str(VENV_DIR)], cwd=CONTROLLER_DIR, check=True)
    else:
        _run([sys.executable, "-m", "venv", str(VENV_DIR)], cwd=CONTROLLER_DIR, check=True)
    return venv_python


def sync_dependencies(venv_python: Path, *, headless: bool):
    token = _dependency_token(headless)
    if STAMP_FILE.exists() and STAMP_FILE.read_text(encoding="utf-8").strip() == token:
        _log("Dependency set unchanged; reusing existing .venv.")
        return

    _log("Refreshing controller dependencies...")
    uv = shutil.which("uv")
    if uv:
        if headless:
            command = [uv, "pip", "install", "--python", str(venv_python), *HEADLESS_PACKAGES]
        else:
            command = [uv, "pip", "install", "--python", str(venv_python), "-r", str(REQUIREMENTS_FILE)]
        _run(command, cwd=CONTROLLER_DIR, check=True)
    else:
        _run([str(venv_python), "-m", "ensurepip", "--upgrade"], cwd=CONTROLLER_DIR)
        command = [str(venv_python), "-m", "pip", "install", "--disable-pip-version-check"]
        if headless:
            command.extend(HEADLESS_PACKAGES)
        else:
            command.extend(["-r", str(REQUIREMENTS_FILE)])
        _run(command, cwd=CONTROLLER_DIR, check=True)

    STAMP_FILE.parent.mkdir(parents=True, exist_ok=True)
    STAMP_FILE.write_text(token + "\n", encoding="utf-8")


def _controller_command(venv_python: Path, args: argparse.Namespace) -> list[str]:
    command = [str(venv_python), str(CONTROLLER_DIR / "controller.py")]
    if args.headless:
        command.append("--headless")
    if args.port:
        command.extend(["--port", args.port])
    return command


def launch_controller(venv_python: Path, args: argparse.Namespace) -> int:
    command = _controller_command(venv_python, args)
    if not args.detached:
        os.execv(str(venv_python), command)

    log_path = _default_log_path()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    child_env = os.environ.copy()
    child_env.setdefault("PYTHONUNBUFFERED", "1")

    with log_path.open("a", encoding="utf-8") as log_file:
        log_file.write("\n[launcher] starting detached controller\n")
        subprocess.Popen(
            command,
            cwd=str(CONTROLLER_DIR),
            stdin=subprocess.DEVNULL,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            env=child_env,
            start_new_session=True,
        )

    _log(f"Detached launch started. Log file: {log_path}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bootstrap launcher for the LeslieLEDs controller")
    parser.add_argument("--detached", action="store_true",
                        help="spawn the controller in the background and exit")
    parser.add_argument("--no-update", action="store_true",
                        help="skip the launch-time git pull check")
    parser.add_argument("--headless", action="store_true",
                        help="launch controller.py in headless bridge mode")
    parser.add_argument("--port", default=None,
                        help="pass through a port substring for headless mode")
    return parser.parse_args()


def main() -> int:
    _prepend_common_mac_paths()
    args = parse_args()

    maybe_fast_forward_update(enabled=not args.no_update)
    venv_python = ensure_venv()
    sync_dependencies(venv_python, headless=args.headless)
    return launch_controller(venv_python, args)


if __name__ == "__main__":
    raise SystemExit(main())