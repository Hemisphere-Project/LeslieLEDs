#!/usr/bin/env python3
"""
LeslieLEDs MIDI Controller
Bridges a DAW's virtual MIDI port to the Midi2DMXnow hardware.
Two run modes:
  * default: DearPyGUI desktop UI with sliders + scene buttons
  * --headless: no GUI, just the virtual port forwarder (production rigs)
"""

import argparse
import os
import signal
import sys
import threading
import time
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

    controller.setup_midi()
    create_gui()

    # Initial port refresh
    refresh_ports()

    # Main loop
    while dpg.is_dearpygui_running():
        dpg.render_dearpygui_frame()

    # Cleanup
    controller.cleanup()
    dpg.destroy_context()
    return 0


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
