#!/usr/bin/env python3
"""
LeslieLEDs MIDI Controller
Bridges a DAW's virtual MIDI port to the Midi2DMXnow hardware.
Two run modes:
  * default: DearPyGUI desktop UI with sliders + scene buttons
  * --headless: no GUI, just the virtual port forwarder (production rigs)
"""

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import rtmidi
import serial
import serial.tools.list_ports

# DearPyGUI is only needed when the UI is shown. Import lazily so the
# headless path runs on machines without an X server / Wayland session.
dpg = None  # type: ignore

# MIDI Configuration
MIDI_CHANNEL = 0  # Channel 1 (0-indexed)

# SysEx protocol constants
SYSEX_MANUFACTURER_ID = 0x7D  # Non-commercial/educational
SYSEX_MSG_STATE_DUMP  = 0x01  # Full state dump message
SYSEX_MSG_RIG_HEALTH  = 0x02  # Slave heartbeat table (2 s interval)
SYSEX_MSG_SCENE_BANK_REQUEST = 0x10
SYSEX_MSG_SCENE_BANK_DUMP = 0x11
SYSEX_MSG_SCENE_BANK_LOAD = 0x12
SYSEX_MSG_SCENE_BANK_STATUS = 0x13

SCENE_BANK_STATUS_OK = 0
SCENE_BANK_STATUS_BAD_VERSION = 1
SCENE_BANK_STATUS_BAD_SIZE = 2
SCENE_BANK_STATUS_DECODE_ERROR = 3
SCENE_BANK_STATUS_APPLY_ERROR = 4

SCENE_BANK_WIRE_VERSION = 1
SCENE_BANK_SCENE_SIZE = 16
SCENE_BANK_FILE_VERSION = 1
SCENE_BANK_FILE_TYPE = "leslieleds-scene-bank"

_SCENE_BANK_STATUS_TEXT = {
    SCENE_BANK_STATUS_OK: "Scene bank applied.",
    SCENE_BANK_STATUS_BAD_VERSION: "Scene bank version mismatch.",
    SCENE_BANK_STATUS_BAD_SIZE: "Scene bank size mismatch.",
    SCENE_BANK_STATUS_DECODE_ERROR: "Scene bank payload decode failed.",
    SCENE_BANK_STATUS_APPLY_ERROR: "Device rejected the scene bank.",
}

# HeartbeatCollector::Status enum (must match heartbeat_collector.h)
SLOT_EMPTY  = 0
SLOT_OK     = 1
SLOT_NO_DMX = 2
SLOT_STALE  = 3
SLOT_LOST   = 4

_STATUS_LABEL = {
    SLOT_EMPTY: "EMPTY",
    SLOT_OK: "OK   ",
    SLOT_NO_DMX: "NO DMX",
    SLOT_STALE: "STALE",
    SLOT_LOST: "LOST ",
}

# ESP reset reason codes (esp_reset_reason_t)
_RESET_REASON = {
    0: "UNK", 1: "POR", 2: "EXT", 3: "SW",
    4: "PANIC", 5: "INT_WDT", 6: "TASK_WDT", 7: "WDT",
    8: "SLEEP", 9: "BROWNOUT", 10: "SDIO",
}

# CC Mappings from config.h
CC_MASTER_BRIGHTNESS = 1
CC_ANIMATION_SPEED = 2
CC_ANIMATION_CTRL = 3
CC_STROBE_RATE = 4
CC_BLEND_MODE = 5
CC_MIRROR_MODE = 6
CC_DIRECTION = 7
CC_ANIMATION_MODE = 8

CC_COLOR_A_HUE = 20
CC_COLOR_A_SATURATION = 21
CC_COLOR_A_VALUE = 22
CC_COLOR_A_WHITE = 23

CC_COLOR_B_HUE = 30
CC_COLOR_B_SATURATION = 31
CC_COLOR_B_VALUE = 32
CC_COLOR_B_WHITE = 33

CC_SCENE_SAVE_MODE = 127

# Note mappings for scenes
# Scenes 1-20: notes 36-55
NOTE_SCENE_1 = 36
MAX_SCENES = 20

def scene_index_to_note(scene_index):
    """Convert scene index (0-19) to MIDI note"""
    return NOTE_SCENE_1 + scene_index  # 36-55

# Default values for all sliders (CC number -> default value)
DEFAULT_VALUES = {
    CC_MASTER_BRIGHTNESS: 64,   # Half brightness
    CC_ANIMATION_SPEED: 64,     # Medium speed
    CC_ANIMATION_CTRL: 0,       # No control offset
    CC_STROBE_RATE: 0,          # No strobe
    CC_BLEND_MODE: 0,           # Default blend
    CC_MIRROR_MODE: 0,          # No mirror
    CC_DIRECTION: 16,           # Forward
    CC_ANIMATION_MODE: 0,       # Solid
    CC_COLOR_A_HUE: 0,          # Red
    CC_COLOR_A_SATURATION: 127, # Full saturation
    CC_COLOR_A_VALUE: 127,      # Full brightness
    CC_COLOR_A_WHITE: 0,        # No white
    CC_COLOR_B_HUE: 64,         # Cyan-ish
    CC_COLOR_B_SATURATION: 127, # Full saturation
    CC_COLOR_B_VALUE: 127,      # Full brightness
    CC_COLOR_B_WHITE: 0,        # No white
}

# Animation modes exposed in the UI (firmware still supports 10 total modes)
ANIM_MODE_COUNT_FIRMWARE = 10
ANIMATION_MODES = [
    ("Solid", 0),
    ("Dual Solid", 1),
    ("Chase", 2),
    ("Dash", 3),
    ("Waveform", 4),
    ("Rainbow", 6),
    ("Sparkle", 7),
]

ANIMATION_MODE_OPTIONS = [label for label, _ in ANIMATION_MODES]

# Mirror modes
MIRROR_MODES = [
    ("None", 0),
    ("Full", 38),
    ("Split 2", 63),
    ("Split 3", 88),
    ("Split 4", 114)
]

# Direction modes
DIRECTION_MODES = [
    ("Forward", 16),
    ("Backward", 48),
    ("Ping Pong", 80),
    ("Random", 112)
]


def _instance_socket_path() -> Path:
    uid = getattr(os, "getuid", lambda: "nouid")()
    return Path(tempfile.gettempdir()) / f"leslieleds_controller_gui_{uid}.sock"


class GuiSingleInstance:
    def __init__(self):
        self.socket_path = _instance_socket_path()
        self._server: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._activate_requested = threading.Event()

    @classmethod
    def activate_existing_instance(cls) -> bool:
        if not hasattr(socket, "AF_UNIX"):
            return False

        socket_path = _instance_socket_path()
        if not socket_path.exists():
            return False

        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(0.5)
                client.connect(str(socket_path))
                client.sendall(b"activate\n")
            return True
        except OSError:
            try:
                socket_path.unlink()
            except FileNotFoundError:
                pass
            except OSError:
                pass
            return False

    def start(self) -> bool:
        if not hasattr(socket, "AF_UNIX"):
            return True

        try:
            self.socket_path.unlink()
        except FileNotFoundError:
            pass
        except OSError:
            pass

        self._server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self._server.bind(str(self.socket_path))
        except OSError:
            self._server.close()
            self._server = None
            return False
        self._server.listen(1)
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()
        return True

    def _serve(self):
        if self._server is None:
            return

        while True:
            try:
                conn, _ = self._server.accept()
            except OSError:
                return

            with conn:
                try:
                    payload = conn.recv(64)
                except OSError:
                    continue

            if b"activate" in payload.lower():
                self._activate_requested.set()

    def consume_activate_request(self) -> bool:
        if not self._activate_requested.is_set():
            return False
        self._activate_requested.clear()
        return True

    def close(self):
        if self._server is not None:
            try:
                self._server.close()
            except OSError:
                pass
            self._server = None

        try:
            self.socket_path.unlink()
        except FileNotFoundError:
            pass
        except OSError:
            pass


def _activate_gui_window():
    if dpg is not None:
        try:
            dpg.show_viewport()
        except Exception:
            pass

    if sys.platform != "darwin":
        return

    script = (
        'tell application "System Events" '
        f'to set frontmost of the first process whose unix id is {os.getpid()} to true'
    )
    try:
        subprocess.run(
            ["/usr/bin/osascript", "-e", script],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=2,
        )
    except Exception:
        pass


class LeslieLEDsController:
    def __init__(self):
        self.midi_out: Optional[rtmidi.MidiOut] = None
        self.midi_in: Optional[rtmidi.MidiIn] = None  # Virtual port for DAW
        self.midi_device_in: Optional[rtmidi.MidiIn] = None  # Input from USB device
        self.midi_port_name: Optional[str] = None
        self.serial_port: Optional[serial.Serial] = None
        self.scene_save_mode = False
        self.virtual_port_thread = None
        self.device_input_thread = None
        self.running = False
        self.is_serial_mode = False
        self._cc_last_time: dict[int, float] = {}
        self._cc_last_value: dict[int, int] = {}
        self._cc_min_interval = 0.004  # seconds between CC transmissions per control
        self._active_scene: int = -1  # Currently active scene (-1 = none)
        self._sysex_buffer: list = []  # Buffer for incoming SysEx
        # Parsed rig health: list of dicts with keys nid, status, fps, reset
        self._rig_health: list = []
        self._last_health_print: float = 0.0  # monotonic time of last terminal print
        self._pending_scene_bank_save_path: Optional[str] = None
        self._pending_scene_bank_operation: Optional[str] = None
        self._pending_scene_bank_deadline: float = 0.0
        # When True, skip every DearPyGUI mutation (no UI is mounted).
        # Set by main_headless(); all _safe_* DPG helpers no-op in that mode.
        self.headless = False

    def _dpg_set(self, tag: str, value):
        if self.headless or dpg is None:
            return
        if dpg.does_item_exist(tag):
            dpg.set_value(tag, value)

    def _dpg_bind_theme(self, tag: str, theme):
        if self.headless or dpg is None:
            return
        if dpg.does_item_exist(tag):
            dpg.bind_item_theme(tag, theme)
        
    def setup_midi(self):
        """Initialize MIDI output and virtual input"""
        self.midi_out = rtmidi.MidiOut()
        self.midi_device_in = rtmidi.MidiIn()
        self.midi_device_in.ignore_types(sysex=False)  # Enable SysEx reception
        
        # Create virtual MIDI IN port for DAW integration with custom client name
        self.midi_in = rtmidi.MidiIn(name="LeslieCTRLs")
        try:
            self.midi_in.open_virtual_port("LeslieCTRLs")
            self.running = True
            # Start thread to forward virtual MIDI IN to output
            self.virtual_port_thread = threading.Thread(target=self._virtual_midi_loop, daemon=True)
            self.virtual_port_thread.start()
        except Exception as e:
            print(f"Could not create virtual MIDI port: {e}")
        
    def _virtual_midi_loop(self):
        """Forward messages from virtual MIDI IN to output or serial"""
        while self.running:
            msg = self.midi_in.get_message()
            if msg:
                midi_message, _ = msg
                
                # Parse CC messages to update GUI sliders (no-op in headless)
                if len(midi_message) == 3 and midi_message[0] == 0xB0 + MIDI_CHANNEL:
                    cc_number = midi_message[1]
                    cc_value = midi_message[2]
                    self._dpg_set(f"cc_{cc_number}_slider", cc_value)
                
                # Forward to Serial MIDI if in serial mode, otherwise USB MIDI
                if self.is_serial_mode:
                    if self.serial_port and self.serial_port.is_open:
                        self._send_serial_midi(midi_message)
                else:
                    if self.midi_out and self.midi_out.is_port_open():
                        self.midi_out.send_message(midi_message)
            time.sleep(0.001)  # Small delay to prevent CPU spinning
    
    def _device_midi_loop(self):
        """Read incoming MIDI from USB device (for SysEx state sync)"""
        while self.running and self.midi_device_in and self.midi_device_in.is_port_open():
            msg = self.midi_device_in.get_message()
            if msg:
                midi_message, _ = msg
                self._process_device_message(midi_message)
            time.sleep(0.001)
    
    def _process_device_message(self, midi_message):
        """Process incoming MIDI message from device"""
        if not midi_message:
            return
        
        # Check for SysEx message (starts with F0, ends with F7)
        if midi_message[0] == 0xF0:
            # Full SysEx message
            if midi_message[-1] == 0xF7:
                self._handle_sysex(midi_message)
            else:
                # Start of multi-part SysEx
                self._sysex_buffer = list(midi_message)
        elif self._sysex_buffer:
            # Continue SysEx
            self._sysex_buffer.extend(midi_message)
            if 0xF7 in midi_message:
                self._handle_sysex(self._sysex_buffer)
                self._sysex_buffer = []
    
    def _handle_sysex(self, sysex_data):
        """Parse and handle SysEx message from device"""
        # Minimum valid message: F0 7D 01 <data> F7
        if len(sysex_data) < 5:
            return
        
        if sysex_data[1] != SYSEX_MANUFACTURER_ID:
            return  # Not our message
        
        msg_type = sysex_data[2]
        
        if msg_type == SYSEX_MSG_STATE_DUMP and len(sysex_data) >= 21:
            # State dump message - update all sliders
            # Data is at indices 3-19 (17 bytes), scaled from 0-127
            self._update_slider_from_sysex(CC_MASTER_BRIGHTNESS, sysex_data[3])
            self._update_slider_from_sysex(CC_ANIMATION_SPEED, sysex_data[4])
            self._update_slider_from_sysex(CC_ANIMATION_CTRL, sysex_data[5])
            self._update_slider_from_sysex(CC_STROBE_RATE, sysex_data[6])
            self._update_slider_from_sysex(CC_BLEND_MODE, sysex_data[7])
            self._update_slider_from_sysex(CC_MIRROR_MODE, sysex_data[8])
            self._update_slider_from_sysex(CC_DIRECTION, sysex_data[9])
            self._update_slider_from_sysex(CC_ANIMATION_MODE, sysex_data[10])
            
            self._update_slider_from_sysex(CC_COLOR_A_HUE, sysex_data[11])
            self._update_slider_from_sysex(CC_COLOR_A_SATURATION, sysex_data[12])
            self._update_slider_from_sysex(CC_COLOR_A_VALUE, sysex_data[13])
            self._update_slider_from_sysex(CC_COLOR_A_WHITE, sysex_data[14])
            
            self._update_slider_from_sysex(CC_COLOR_B_HUE, sysex_data[15])
            self._update_slider_from_sysex(CC_COLOR_B_SATURATION, sysex_data[16])
            self._update_slider_from_sysex(CC_COLOR_B_VALUE, sysex_data[17])
            self._update_slider_from_sysex(CC_COLOR_B_WHITE, sysex_data[18])
            
            # Update active scene indicator
            scene = sysex_data[19] if len(sysex_data) > 19 else 127
            self._update_active_scene(scene if scene < MAX_SCENES else -1)

        elif msg_type == SYSEX_MSG_RIG_HEALTH and len(sysex_data) >= 4:
            # Rig health table.  Format: F0 7D 02 <count> [N×6 bytes] F7
            count = sysex_data[3]
            slots = []
            for i in range(count):
                base = 4 + i * 6
                if base + 5 >= len(sysex_data):
                    break
                slots.append({
                    "nid":    (sysex_data[base], sysex_data[base+1], sysex_data[base+2]),
                    "status": sysex_data[base+3],
                    "fps":    sysex_data[base+4],
                    "reset":  sysex_data[base+5],
                })
            self._rig_health = slots
            self._update_rig_health_display()
            if self.headless:
                self._print_rig_health_if_due()

        elif msg_type == SYSEX_MSG_SCENE_BANK_DUMP and len(sysex_data) >= 7:
            self._handle_scene_bank_dump(sysex_data)

        elif msg_type == SYSEX_MSG_SCENE_BANK_STATUS and len(sysex_data) >= 5:
            self._handle_scene_bank_status(sysex_data)

    def _update_rig_health_display(self):
        """Update rig-health text rows in the GUI. No-op in headless."""
        if self.headless or dpg is None:
            return
        color_map = {
            SLOT_OK:     (0,   200,   0, 255),
            SLOT_NO_DMX: (220, 120,   0, 255),
            SLOT_STALE:  (220, 180,   0, 255),
            SLOT_LOST:   (220,  50,  50, 255),
            SLOT_EMPTY:  (80,   80,  80, 150),
        }
        for i in range(8):
            tag = f"hb_dot_{i}"
            if not dpg.does_item_exist(tag):
                continue
            if i < len(self._rig_health):
                slot = self._rig_health[i]
                nid = slot["nid"]
                nid_str = f"{nid[0]:02X}:{nid[1]:02X}:{nid[2]:02X}"
                status_label = _STATUS_LABEL.get(slot["status"], "?    ").strip()
                rst = _RESET_REASON.get(slot["reset"], str(slot["reset"]))
                text = f"  {nid_str}  {status_label:<5}  {slot['fps']:3d}fps  {rst}"
                col = color_map.get(slot["status"], color_map[SLOT_EMPTY])
            else:
                text = "  --:--:--"
                col = color_map[SLOT_EMPTY]
            dpg.configure_item(tag, default_value=text, color=col)

    def _print_rig_health_if_due(self, interval: float = 5.0):
        """Print a rig-health table to stdout in headless mode (throttled)."""
        now = time.monotonic()
        if now - self._last_health_print < interval:
            return
        self._last_health_print = now
        if not self._rig_health:
            print("[rig] no slaves heard yet")
            return
        print("[rig] NID        STATUS   FPS  RST")
        for s in self._rig_health:
            nid = s["nid"]
            nid_str = f"{nid[0]:02X}:{nid[1]:02X}:{nid[2]:02X}"
            status  = _STATUS_LABEL.get(s["status"], "?    ")
            fps     = s["fps"]
            rst     = _RESET_REASON.get(s["reset"], str(s["reset"]))
            print(f"[rig]  {nid_str}  {status}  {fps:3d}fps  {rst}")

    def _update_slider_from_sysex(self, cc_number, value):
        """Update a slider from SysEx data (value is 0-127). No-op in headless."""
        self._dpg_set(f"cc_{cc_number}_slider", value)

        # Also update combo boxes for mode/mirror/direction
        if cc_number == CC_ANIMATION_MODE:
            self._update_combo_from_value("anim_mode_combo", ANIMATION_MODES, value)
        elif cc_number == CC_MIRROR_MODE:
            self._update_combo_from_value("mirror_mode_combo", MIRROR_MODES, value)
        elif cc_number == CC_DIRECTION:
            self._update_combo_from_value("direction_mode_combo", DIRECTION_MODES, value)

    def _update_combo_from_value(self, combo_id, modes_list, value):
        """Update a combo box selection based on CC value. No-op in headless."""
        if self.headless or dpg is None:
            return
        if not dpg.does_item_exist(combo_id):
            return
        # Find closest matching mode
        best_match = modes_list[0][0]
        best_diff = 255
        for name, mode_val in modes_list:
            diff = abs(mode_val - value)
            if diff < best_diff:
                best_diff = diff
                best_match = name
        dpg.set_value(combo_id, best_match)

    def _update_active_scene(self, scene_index):
        """Update the active scene indicator on buttons. No-op in headless."""
        self._active_scene = scene_index
        if self.headless or dpg is None:
            return
        try:
            for i in range(MAX_SCENES):
                if self.scene_save_mode:
                    self._dpg_bind_theme(f"scene_btn_{i}", "save_mode_theme")
                elif i == scene_index:
                    self._dpg_bind_theme(f"scene_btn_{i}", "active_scene_theme")
                else:
                    self._dpg_bind_theme(f"scene_btn_{i}", 0)
        except Exception:
            pass  # GUI not ready yet

    def _default_scene_bank_path(self) -> str:
        return str(Path.cwd() / "leslie_scene_bank.json")

    def _update_scene_bank_status(self, message: str, color=(150, 150, 150)):
        if not self.headless:
            print(f"[scene-bank] {message}")
        if self.headless or dpg is None:
            return
        if dpg.does_item_exist("scene_bank_status_text"):
            dpg.set_value("scene_bank_status_text", message)
            dpg.configure_item("scene_bank_status_text", color=color)

    @staticmethod
    def _encode_nibble_bytes(raw: bytes) -> list[int]:
        encoded: list[int] = []
        for value in raw:
            encoded.append((value >> 4) & 0x0F)
            encoded.append(value & 0x0F)
        return encoded

    @staticmethod
    def _decode_nibble_bytes(encoded: list[int]) -> Optional[bytes]:
        if len(encoded) % 2 != 0:
            return None
        raw = bytearray(len(encoded) // 2)
        for index in range(0, len(encoded), 2):
            hi = encoded[index]
            lo = encoded[index + 1]
            if hi < 0 or hi > 0x0F or lo < 0 or lo > 0x0F:
                return None
            raw[index // 2] = (hi << 4) | lo
        return bytes(raw)

    def _clear_scene_bank_pending(self):
        self._pending_scene_bank_operation = None
        self._pending_scene_bank_save_path = None
        self._pending_scene_bank_deadline = 0.0

    def _scene_bank_transport_ready(self) -> bool:
        if self.is_serial_mode:
            self._update_scene_bank_status(
                "Scene bank backup uses USB MIDI only; serial mode ignores SysEx.",
                (255, 140, 100),
            )
            return False
        if not self.midi_out or not self.midi_out.is_port_open():
            self._update_scene_bank_status("Connect the device over USB MIDI first.", (255, 100, 100))
            return False
        if not self.midi_device_in or not self.midi_device_in.is_port_open():
            self._update_scene_bank_status(
                "USB MIDI input is not open; bank transfer needs bidirectional MIDI.",
                (255, 100, 100),
            )
            return False
        return True

    def send_sysex(self, body: list[int]) -> bool:
        if not self._scene_bank_transport_ready():
            return False
        if not self.midi_out or not self.midi_out.is_port_open():
            return False
        self.midi_out.send_message([0xF0, SYSEX_MANUFACTURER_ID, *body, 0xF7])
        return True

    def request_scene_bank_dump(self, file_path: str):
        target = Path(file_path or self._default_scene_bank_path()).expanduser()
        if not self.send_sysex([SYSEX_MSG_SCENE_BANK_REQUEST, SCENE_BANK_WIRE_VERSION]):
            return
        self._pending_scene_bank_save_path = str(target)
        self._pending_scene_bank_operation = "dump"
        self._pending_scene_bank_deadline = time.monotonic() + 5.0
        self._update_scene_bank_status(f"Requesting scene bank into {target} ...", (120, 180, 255))

    def load_scene_bank_from_file(self, file_path: str):
        target = Path(file_path or self._default_scene_bank_path()).expanduser()
        if not target.exists():
            self._update_scene_bank_status(f"Scene bank file not found: {target}", (255, 100, 100))
            return

        try:
            payload = json.loads(target.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            self._update_scene_bank_status(f"Could not read scene bank file: {exc}", (255, 100, 100))
            return

        if payload.get("file_type") != SCENE_BANK_FILE_TYPE:
            self._update_scene_bank_status("Scene bank file type is not recognized.", (255, 100, 100))
            return

        wire_version = int(payload.get("wire_version", 0))
        scene_count = int(payload.get("scene_count", 0))
        scene_size = int(payload.get("scene_size", 0))
        data_hex = payload.get("data_hex")

        if wire_version != SCENE_BANK_WIRE_VERSION:
            self._update_scene_bank_status("Scene bank file uses an unsupported wire version.", (255, 100, 100))
            return
        if scene_count <= 0 or scene_count > 127 or scene_size != SCENE_BANK_SCENE_SIZE:
            self._update_scene_bank_status("Scene bank file has invalid size metadata.", (255, 100, 100))
            return
        if not isinstance(data_hex, str):
            self._update_scene_bank_status("Scene bank file is missing raw bank data.", (255, 100, 100))
            return

        try:
            raw = bytes.fromhex(data_hex)
        except ValueError as exc:
            self._update_scene_bank_status(f"Scene bank hex payload is invalid: {exc}", (255, 100, 100))
            return

        expected_size = scene_count * scene_size
        if len(raw) != expected_size:
            self._update_scene_bank_status(
                f"Scene bank payload length mismatch: expected {expected_size} bytes, got {len(raw)}.",
                (255, 100, 100),
            )
            return

        message = [
            SYSEX_MSG_SCENE_BANK_LOAD,
            wire_version,
            scene_count,
            scene_size,
            *self._encode_nibble_bytes(raw),
        ]
        if not self.send_sysex(message):
            return

        self._pending_scene_bank_operation = "load"
        self._pending_scene_bank_deadline = time.monotonic() + 5.0
        self._update_scene_bank_status(f"Uploading scene bank from {target} ...", (120, 180, 255))

    def _handle_scene_bank_dump(self, sysex_data):
        wire_version = sysex_data[3]
        scene_count = sysex_data[4]
        scene_size = sysex_data[5]
        encoded = sysex_data[6:-1]

        if wire_version != SCENE_BANK_WIRE_VERSION:
            self._clear_scene_bank_pending()
            self._update_scene_bank_status("Device returned an unsupported scene bank version.", (255, 100, 100))
            return
        if scene_count <= 0 or scene_size != SCENE_BANK_SCENE_SIZE:
            self._clear_scene_bank_pending()
            self._update_scene_bank_status("Device returned invalid scene bank metadata.", (255, 100, 100))
            return

        raw = self._decode_nibble_bytes(encoded)
        if raw is None:
            self._clear_scene_bank_pending()
            self._update_scene_bank_status("Device returned a corrupt scene bank payload.", (255, 100, 100))
            return

        expected_size = scene_count * scene_size
        if len(raw) != expected_size:
            self._clear_scene_bank_pending()
            self._update_scene_bank_status(
                f"Device returned {len(raw)} bytes, expected {expected_size} for the scene bank.",
                (255, 100, 100),
            )
            return

        target = Path(self._pending_scene_bank_save_path or self._default_scene_bank_path()).expanduser()
        target.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "file_type": SCENE_BANK_FILE_TYPE,
            "file_version": SCENE_BANK_FILE_VERSION,
            "wire_version": wire_version,
            "scene_count": scene_count,
            "scene_size": scene_size,
            "captured_at": datetime.now(timezone.utc).isoformat(),
            "source_port": self.midi_port_name,
            "data_hex": raw.hex(),
        }

        try:
            target.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        except OSError as exc:
            self._clear_scene_bank_pending()
            self._update_scene_bank_status(f"Could not write scene bank file: {exc}", (255, 100, 100))
            return

        self._clear_scene_bank_pending()
        self._update_scene_bank_status(f"Saved full scene bank to {target}", (100, 220, 120))

    def _handle_scene_bank_status(self, sysex_data):
        status_code = sysex_data[3]
        detail = sysex_data[4] if len(sysex_data) > 4 else 0
        base_message = _SCENE_BANK_STATUS_TEXT.get(status_code, f"Unknown scene bank status {status_code}.")

        if status_code == SCENE_BANK_STATUS_OK:
            if self._pending_scene_bank_operation == "load":
                message = f"Scene bank applied on device ({detail} scenes)."
            else:
                message = base_message
            color = (100, 220, 120)
        elif status_code == SCENE_BANK_STATUS_BAD_VERSION:
            message = f"{base_message} Device detail: {detail}."
            color = (255, 100, 100)
        elif status_code == SCENE_BANK_STATUS_BAD_SIZE:
            message = f"{base_message} Device detail: {detail}."
            color = (255, 100, 100)
        else:
            message = base_message
            color = (255, 100, 100)

        if self._pending_scene_bank_operation == "load" or status_code != SCENE_BANK_STATUS_OK:
            self._clear_scene_bank_pending()
        self._update_scene_bank_status(message, color)

    def poll_pending_operations(self):
        if not self._pending_scene_bank_operation:
            return
        if time.monotonic() < self._pending_scene_bank_deadline:
            return

        operation = self._pending_scene_bank_operation
        self._clear_scene_bank_pending()
        self._update_scene_bank_status(f"Scene bank {operation} timed out.", (255, 140, 100))
        
    def get_available_ports(self):
        """Get list of available MIDI output ports and serial ports"""
        ports = []
        
        # Add MIDI ports (exclude our own virtual port)
        if self.midi_out:
            midi_ports = self.midi_out.get_ports()
            for port in midi_ports:
                # Skip our own virtual MIDI IN port to avoid loops
                if "RtMidiIn Client" not in port and "LeslieCTRLs" not in port:
                    ports.append(("MIDI", port))
        
        # Add serial ports (look for LeslieLEDs or M5Stack devices)
        serial_ports = serial.tools.list_ports.comports()
        for port in serial_ports:
            # Show port with description
            port_label = f"Serial: {port.device}"
            if port.description and port.description != "n/a":
                port_label += f" ({port.description})"
            ports.append(("SERIAL", port.device, port_label))
        
        return ports
    
    def connect_port(self, port_type: str, port_identifier):
        """Connect to a MIDI or Serial port"""
        # Close existing connections
        if self.midi_out and self.midi_out.is_port_open():
            self.midi_out.close_port()
        if self.midi_device_in and self.midi_device_in.is_port_open():
            self.midi_device_in.close_port()
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            self.serial_port = None
        self._clear_scene_bank_pending()
        
        if port_type == "MIDI":
            # Connect to USB MIDI port (both output and input)
            if self.midi_out:
                midi_out_ports = self.midi_out.get_ports()
                if port_identifier in midi_out_ports:
                    port_index = midi_out_ports.index(port_identifier)
                    self.midi_out.open_port(port_index)
                    self.midi_port_name = port_identifier
                    self.is_serial_mode = False
                    
                    # Also open input port from the same device
                    if self.midi_device_in:
                        midi_in_ports = self.midi_device_in.get_ports()
                        # Find matching input port (same device name)
                        for i, in_port in enumerate(midi_in_ports):
                            # Match by device name (e.g., "Midi2DMXnow")
                            if "Midi2DMXnow" in in_port or port_identifier.split()[0] in in_port:
                                self.midi_device_in.open_port(i)
                                # Start device input thread
                                if self.device_input_thread is None or not self.device_input_thread.is_alive():
                                    self.device_input_thread = threading.Thread(
                                        target=self._device_midi_loop, daemon=True)
                                    self.device_input_thread.start()
                                break
                    return True
        elif port_type == "SERIAL":
            # Connect to Serial MIDI port
            try:
                self.serial_port = serial.Serial(
                    port=port_identifier,
                    baudrate=115200,
                    timeout=0.01
                )
                self.midi_port_name = port_identifier
                self.is_serial_mode = True
                return True
            except Exception as e:
                print(f"Serial connection failed: {e}")
                return False
        
        return False
    
    def _send_serial_midi(self, midi_message):
        """Send raw MIDI bytes to serial port"""
        if self.serial_port and self.serial_port.is_open:
            try:
                # Send raw MIDI bytes
                self.serial_port.write(bytes(midi_message))
            except Exception as e:
                print(f"Serial MIDI send error: {e}")
    
    def send_cc(self, cc_number: int, value: int):
        """Send MIDI CC message"""
        value = max(0, min(127, int(value)))
        
        now = time.monotonic()
        last_time = self._cc_last_time.get(cc_number, -1.0)
        last_value = self._cc_last_value.get(cc_number)
        
        if last_value == value and now - last_time < self._cc_min_interval:
            return
        
        if last_value is not None and now - last_time < self._cc_min_interval:
            if abs(value - last_value) <= 1:
                return
        
        self._cc_last_time[cc_number] = now
        self._cc_last_value[cc_number] = value
        message = [0xB0 + MIDI_CHANNEL, cc_number, value]
        
        if self.is_serial_mode and self.serial_port:
            self._send_serial_midi(message)
        elif self.midi_out and self.midi_out.is_port_open():
            self.midi_out.send_message(message)
    
    def send_note(self, note: int, velocity: int = 127):
        """Send MIDI note on message"""
        message = [0x90 + MIDI_CHANNEL, note, velocity]
        
        if self.is_serial_mode and self.serial_port:
            self._send_serial_midi(message)
        elif self.midi_out and self.midi_out.is_port_open():
            self.midi_out.send_message(message)
            
    def cleanup(self):
        """Clean up MIDI and serial resources"""
        self.running = False
        if self.virtual_port_thread:
            self.virtual_port_thread.join(timeout=1.0)
        if self.midi_out and self.midi_out.is_port_open():
            self.midi_out.close_port()
        if self.midi_in:
            del self.midi_in
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()


# Global controller instance
controller = LeslieLEDsController()

# Global port mapping (can't store dicts in DPG value registry reliably)
port_map = {}


def refresh_ports():
    """Refresh MIDI and Serial port list"""
    global port_map
    
    ports = controller.get_available_ports()
    
    # Create display labels
    port_labels = []
    port_map = {}  # Map label to (type, identifier)
    
    for port_info in ports:
        if port_info[0] == "MIDI":
            label = f"MIDI: {port_info[1]}"
            port_labels.append(label)
            port_map[label] = ("MIDI", port_info[1])
        elif port_info[0] == "SERIAL":
            label = port_info[2]  # Already formatted with description
            port_labels.append(label)
            port_map[label] = ("SERIAL", port_info[1])
    
    dpg.configure_item("port_combo", items=port_labels)
    
    def attempt_connect(label: str) -> bool:
        port_type, port_id = port_map[label]
        if controller.connect_port(port_type, port_id):
            mode_text = "Serial MIDI" if port_type == "SERIAL" else "USB MIDI"
            dpg.set_value("status_text", f"Connected ({mode_text}): {label}")
            dpg.configure_item("status_text", color=(100, 255, 100))
            return True
        return False

    preferred_keywords = ["midi2dmxnow", "leslieleds", "m5stack"]
    for keyword in preferred_keywords:
        for label in port_labels:
            if keyword in label.lower():
                dpg.set_value("port_combo", label)
                if attempt_connect(label):
                    return
    
    # If no LeslieLEDs found, try to find "USB Single Serial" port
    for label in port_labels:
        if "USB Single Serial" in label:
            dpg.set_value("port_combo", label)
            if attempt_connect(label):
                return
    
    # If no auto-select, just select first port if available
    if port_labels:
        dpg.set_value("port_combo", port_labels[0])


def connect_midi(sender, app_data):
    """Connect to selected MIDI or Serial port"""
    global port_map
    
    port_label = dpg.get_value("port_combo")
    
    if port_label and port_label in port_map:
        port_type, port_id = port_map[port_label]
        if controller.connect_port(port_type, port_id):
            mode_text = "Serial MIDI" if port_type == "SERIAL" else "USB MIDI"
            dpg.set_value("status_text", f"Connected ({mode_text}): {port_label}")
            dpg.configure_item("status_text", color=(100, 255, 100))
        else:
            dpg.set_value("status_text", "Connection failed")
            dpg.configure_item("status_text", color=(255, 100, 100))


def on_cc_slider(sender, app_data, user_data):
    """Handle CC slider change"""
    cc_number = user_data
    value = int(app_data)
    controller.send_cc(cc_number, value)


def on_animation_mode(sender, app_data):
    """Handle animation mode selection"""
    label_to_mode = {label: mode for label, mode in ANIMATION_MODES}
    mode_index = label_to_mode.get(app_data)
    if mode_index is None:
        return

    valid_values = [
        value for value in range(128)
        if ((value * ANIM_MODE_COUNT_FIRMWARE + 64) // 128) == mode_index
    ]
    if not valid_values:
        cc_value = 0
    else:
        cc_value = valid_values[len(valid_values) // 2]
    controller.send_cc(CC_ANIMATION_MODE, cc_value)


def on_mirror_mode(sender, app_data):
    """Handle mirror mode selection"""
    for name, value in MIRROR_MODES:
        if name == app_data:
            controller.send_cc(CC_MIRROR_MODE, value)
            break


def on_direction_mode(sender, app_data):
    """Handle direction mode selection"""
    for name, value in DIRECTION_MODES:
        if name == app_data:
            controller.send_cc(CC_DIRECTION, value)
            break


def on_scene_button(sender, app_data, user_data):
    """Handle scene button press"""
    scene_note = scene_index_to_note(user_data)
    controller.send_note(scene_note)
    
    # Auto-unarm save mode after clicking a scene button
    if controller.scene_save_mode:
        controller.scene_save_mode = False
        controller.send_cc(CC_SCENE_SAVE_MODE, 0)
        # Update the checkbox in the GUI
        dpg.set_value("save_mode_checkbox", False)
        update_scene_button_colors()


def on_dump_scene_bank(sender=None, app_data=None, user_data=None):
    """Request a full scene-bank dump from the device and save it on the host."""
    file_path = dpg.get_value("scene_bank_path") if dpg is not None else ""
    controller.request_scene_bank_dump(file_path)


def on_load_scene_bank(sender=None, app_data=None, user_data=None):
    """Load a full scene-bank file from disk and push it to the device."""
    file_path = dpg.get_value("scene_bank_path") if dpg is not None else ""
    controller.load_scene_bank_from_file(file_path)


def on_reset_button():
    """Reset all sliders to default values and send to MIDI receiver"""
    # Map CC numbers to their slider tags
    cc_to_slider = {
        CC_MASTER_BRIGHTNESS: "cc_1_slider",
        CC_ANIMATION_SPEED: "cc_2_slider",
        CC_ANIMATION_CTRL: "cc_3_slider",
        CC_STROBE_RATE: "cc_4_slider",
        CC_BLEND_MODE: "cc_5_slider",
        CC_COLOR_A_HUE: "cc_20_slider",
        CC_COLOR_A_SATURATION: "cc_21_slider",
        CC_COLOR_A_VALUE: "cc_22_slider",
        CC_COLOR_A_WHITE: "cc_23_slider",
        CC_COLOR_B_HUE: "cc_30_slider",
        CC_COLOR_B_SATURATION: "cc_31_slider",
        CC_COLOR_B_VALUE: "cc_32_slider",
        CC_COLOR_B_WHITE: "cc_33_slider",
    }
    
    # Reset each slider and send CC
    for cc_number, default_value in DEFAULT_VALUES.items():
        # Update slider if it exists
        slider_tag = cc_to_slider.get(cc_number)
        if slider_tag and dpg.does_item_exist(slider_tag):
            dpg.set_value(slider_tag, default_value)
        
        # Send CC value
        controller.send_cc(cc_number, default_value)
    
    # Reset animation mode combo
    dpg.set_value("animation_mode_combo", ANIMATION_MODE_OPTIONS[0])
    
    # Reset mirror mode combo
    dpg.set_value("mirror_mode_combo", MIRROR_MODES[0][0])
    
    # Reset direction mode combo
    dpg.set_value("direction_mode_combo", DIRECTION_MODES[0][0])


def toggle_scene_save_mode(sender, app_data):
    """Toggle scene save mode"""
    controller.scene_save_mode = app_data
    value = 64 if app_data else 0  # >37 = save mode, 0 = load mode
    controller.send_cc(CC_SCENE_SAVE_MODE, value)
    update_scene_button_colors()


def update_scene_button_colors():
    """Update scene button colors based on save mode and active scene"""
    for i in range(20):
        if controller.scene_save_mode:
            # Red color for save mode
            dpg.bind_item_theme(f"scene_btn_{i}", "save_mode_theme")
        elif i == controller._active_scene:
            # Green for active scene
            dpg.bind_item_theme(f"scene_btn_{i}", "active_scene_theme")
        else:
            # Default theme
            dpg.bind_item_theme(f"scene_btn_{i}", 0)


def create_gui():
    """Create the DearPyGUI interface"""
    dpg.create_context()
    
    # Create theme for save mode (red buttons)
    with dpg.theme(tag="save_mode_theme"):
        with dpg.theme_component(dpg.mvButton):
            dpg.add_theme_color(dpg.mvThemeCol_Button, (180, 40, 40, 255))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (220, 60, 60, 255))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (150, 30, 30, 255))
    
    # Create theme for active scene (green buttons)
    with dpg.theme(tag="active_scene_theme"):
        with dpg.theme_component(dpg.mvButton):
            dpg.add_theme_color(dpg.mvThemeCol_Button, (40, 140, 40, 255))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonHovered, (60, 180, 60, 255))
            dpg.add_theme_color(dpg.mvThemeCol_ButtonActive, (30, 110, 30, 255))
    
    # Main window
    with dpg.window(label="LeslieLEDs Controller", tag="main_window"):
        
        # MIDI Connection section
        with dpg.group(horizontal=True):
            dpg.add_text("Output Port:")
            dpg.add_combo([], tag="port_combo", width=400, callback=connect_midi)
            dpg.add_button(label="Refresh", callback=refresh_ports)
            dpg.add_button(label="Connect", callback=connect_midi)
        
        dpg.add_text("Not connected", tag="status_text", color=(255, 100, 100))
        
        dpg.add_spacer(height=5)
        with dpg.group(horizontal=True):
            dpg.add_text("Virtual MIDI IN:", color=(100, 200, 255))
            dpg.add_text("LeslieLEDs Controller", color=(150, 150, 150))
        dpg.add_text("Use this port in your DAW to send MIDI", color=(120, 120, 120))
        
        dpg.add_separator()
        
        # Global Controls
        with dpg.collapsing_header(label="Global Controls", default_open=True):
            
            dpg.add_text("Animation Mode:")
            dpg.add_combo(ANIMATION_MODE_OPTIONS, tag="animation_mode_combo",
                         default_value=ANIMATION_MODE_OPTIONS[0], 
                         callback=on_animation_mode, width=200)
            
            dpg.add_spacer(height=5)
            dpg.add_text("Master Brightness:")
            dpg.add_slider_int(label="##brightness", tag="cc_1_slider", default_value=64, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_MASTER_BRIGHTNESS, width=300)
            
            dpg.add_text("Animation Speed:")
            dpg.add_slider_int(label="##speed", tag="cc_2_slider", default_value=64, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_ANIMATION_SPEED, width=300)
            
            dpg.add_text("Animation Control:")
            dpg.add_slider_int(label="##ctrl", tag="cc_3_slider", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_ANIMATION_CTRL, width=300)
            
            dpg.add_text("Strobe Rate (0=off):")
            dpg.add_slider_int(label="##strobe", tag="cc_4_slider", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_STROBE_RATE, width=300)
            
            dpg.add_text("Blend Mode:")
            dpg.add_slider_int(label="##blend", tag="cc_5_slider", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_BLEND_MODE, width=300)
            
            dpg.add_text("Mirror Mode:")
            dpg.add_combo([name for name, _ in MIRROR_MODES], tag="mirror_mode_combo",
                         default_value=MIRROR_MODES[0][0],
                         callback=on_mirror_mode, width=200)
            
            dpg.add_text("Direction:")
            dpg.add_combo([name for name, _ in DIRECTION_MODES], tag="direction_mode_combo",
                         default_value=DIRECTION_MODES[0][0],
                         callback=on_direction_mode, width=200)
        
        dpg.add_separator()
        
        # Color A Controls
        with dpg.collapsing_header(label="Color A (RGBW)", default_open=True):
            dpg.add_text("Hue:")
            dpg.add_slider_int(label="##a_hue", tag="cc_20_slider", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_HUE, width=300)
            
            dpg.add_text("Saturation:")
            dpg.add_slider_int(label="##a_sat", tag="cc_21_slider", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_SATURATION, width=300)
            
            dpg.add_text("Value/Brightness:")
            dpg.add_slider_int(label="##a_val", tag="cc_22_slider", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_VALUE, width=300)
            
            dpg.add_text("White:")
            dpg.add_slider_int(label="##a_white", tag="cc_23_slider", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_WHITE, width=300)
        
        dpg.add_separator()
        
        # Color B Controls
        with dpg.collapsing_header(label="Color B (RGBW)", default_open=True):
            dpg.add_text("Hue:")
            dpg.add_slider_int(label="##b_hue", tag="cc_30_slider", default_value=64, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_B_HUE, width=300)
            
            dpg.add_text("Saturation:")
            dpg.add_slider_int(label="##b_sat", tag="cc_31_slider", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_B_SATURATION, width=300)
            
            dpg.add_text("Value:")
            dpg.add_slider_int(label="##b_val", tag="cc_32_slider", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_B_VALUE, width=300)
            
            dpg.add_text("White:")
            dpg.add_slider_int(label="##b_white", tag="cc_33_slider", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_B_WHITE, width=300)
        
        dpg.add_separator()
        
        # Scene Management
        with dpg.collapsing_header(label="Scenes", default_open=True):
            
            dpg.add_checkbox(label="Scene Save Mode (>37 to save)", 
                           callback=toggle_scene_save_mode,
                           tag="save_mode_checkbox")
            
            dpg.add_spacer(height=5)
            dpg.add_text("Scene Buttons:")
            
            # Scene buttons in four rows of 5
            with dpg.group(horizontal=True):
                for i in range(5):
                    dpg.add_button(label=f"{i+1}", 
                                 callback=on_scene_button, 
                                 user_data=i,
                                 width=60,
                                 tag=f"scene_btn_{i}")
            
            with dpg.group(horizontal=True):
                for i in range(5, 10):
                    dpg.add_button(label=f"{i+1}", 
                                 callback=on_scene_button, 
                                 user_data=i,
                                 width=60,
                                 tag=f"scene_btn_{i}")
            
            with dpg.group(horizontal=True):
                for i in range(10, 15):
                    dpg.add_button(label=f"{i+1}", 
                                 callback=on_scene_button, 
                                 user_data=i,
                                 width=60,
                                 tag=f"scene_btn_{i}")
            
            with dpg.group(horizontal=True):
                for i in range(15, 20):
                    dpg.add_button(label=f"{i+1}", 
                                 callback=on_scene_button, 
                                 user_data=i,
                                 width=60,
                                 tag=f"scene_btn_{i}")
            
            dpg.add_spacer(height=10)
            dpg.add_text("Scene Bank Backup:")
            dpg.add_input_text(tag="scene_bank_path",
                               default_value=controller._default_scene_bank_path(),
                               width=360)
            with dpg.group(horizontal=True):
                dpg.add_button(label="Dump Bank", callback=on_dump_scene_bank, width=110)
                dpg.add_button(label="Load Bank", callback=on_load_scene_bank, width=110)
            dpg.add_text("USB MIDI only. Saves/restores the full device preset bank as one host file.",
                         color=(120, 120, 120), wrap=420)
            dpg.add_text("", tag="scene_bank_status_text", color=(150, 150, 150), wrap=420)

            dpg.add_spacer(height=10)
            dpg.add_button(label="RESET", callback=on_reset_button,
                          width=200, height=30)

        dpg.add_separator()

        # Rig health: one line per slave slot, colour-coded by heartbeat status.
        # Updated by _update_rig_health_display() whenever a SYSEX_MSG_RIG_HEALTH
        # arrives from the master (every 2 s).
        with dpg.collapsing_header(label="Rig Health", default_open=True):
            dpg.add_text("Slave nodes (live via SysEx, 2 s interval):",
                         color=(120, 120, 120))
            for i in range(8):
                dpg.add_text("  --:--:--", tag=f"hb_dot_{i}",
                             color=(80, 80, 80, 150))

    dpg.create_viewport(title="LeslieLEDs Controller", width=500, height=1050)
    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("main_window", True)


def _headless_select_port(prefer_label: Optional[str]) -> bool:
    """Pick a hardware MIDI/serial port for headless mode.

    With --port: only attempt that label (substring match against the
    formatted port list). Without: try the same auto-select keywords
    the GUI uses ("midi2dmxnow", "leslieleds", "m5stack", then
    "USB Single Serial"). Returns True on success.
    """
    ports = controller.get_available_ports()
    labelled = []
    for port_info in ports:
        if port_info[0] == "MIDI":
            labelled.append(("MIDI", port_info[1], f"MIDI: {port_info[1]}"))
        elif port_info[0] == "SERIAL":
            labelled.append(("SERIAL", port_info[1], port_info[2]))

    candidates = []
    if prefer_label:
        needle = prefer_label.lower()
        candidates = [c for c in labelled if needle in c[2].lower()]
    else:
        for keyword in ("midi2dmxnow", "leslieleds", "m5stack"):
            candidates.extend([c for c in labelled if keyword in c[2].lower()])
        candidates.extend([c for c in labelled if "USB Single Serial" in c[2]])

    for port_type, port_id, label in candidates:
        if controller.connect_port(port_type, port_id):
            print(f"[LeslieLEDs] connected to {label}")
            return True

    return False


def main_headless(prefer_port: Optional[str]) -> int:
    """Headless bridge: virtual MIDI port forwarded to hardware, no UI.

    Suitable for production rigs driven from a DAW where the GUI would
    only get in the way (and might not even start without a display).
    """
    controller.headless = True
    controller.setup_midi()
    print("[LeslieLEDs] headless mode")
    print("[LeslieLEDs] virtual port name: LeslieCTRLs")

    if not _headless_select_port(prefer_port):
        print("[LeslieLEDs] WARN: no hardware port matched; messages will be dropped.")
        print("[LeslieLEDs]       available ports:")
        for port_info in controller.get_available_ports():
            if port_info[0] == "MIDI":
                print(f"[LeslieLEDs]         MIDI: {port_info[1]}")
            else:
                print(f"[LeslieLEDs]         {port_info[2]}")

    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    print("[LeslieLEDs] running. Ctrl-C to stop.")
    try:
        while not stop.is_set():
            stop.wait(timeout=0.5)
    finally:
        controller.cleanup()
        print("[LeslieLEDs] stopped.")
    return 0


def main_gui() -> int:
    """Main entry point — desktop UI."""
    global dpg
    import dearpygui.dearpygui as _dpg
    dpg = _dpg

    if GuiSingleInstance.activate_existing_instance():
        return 0

    single_instance = GuiSingleInstance()
    if not single_instance.start():
        if GuiSingleInstance.activate_existing_instance():
            return 0
        print("[LeslieLEDs] another GUI instance is starting; exiting.")
        return 0
    context_created = False

    try:
        controller.setup_midi()
        create_gui()
        context_created = True

        # Initial port refresh
        refresh_ports()

        # Main loop
        while dpg.is_dearpygui_running():
            if single_instance.consume_activate_request():
                _activate_gui_window()
            controller.poll_pending_operations()
            dpg.render_dearpygui_frame()

        return 0
    finally:
        controller.cleanup()
        single_instance.close()
        if context_created:
            dpg.destroy_context()


def main():
    parser = argparse.ArgumentParser(description="LeslieLEDs MIDI bridge / controller")
    parser.add_argument("--headless", action="store_true",
                        help="run without GUI; just bridge LeslieCTRLs virtual MIDI to hardware")
    parser.add_argument("--port", default=None,
                        help="(headless) substring of the port label to connect to; "
                             "if omitted, auto-selects a Midi2DMXnow / M5Stack / "
                             "USB Single Serial device")
    args = parser.parse_args()

    if args.headless:
        return main_headless(args.port)
    return main_gui()


if __name__ == "__main__":
    sys.exit(main())
