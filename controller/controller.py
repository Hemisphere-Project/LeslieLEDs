#!/usr/bin/env python3
"""
LeslieLEDs MIDI Controller
Simple DearPyGUI interface for controlling LeslieLEDs without a DAW
Supports both USB MIDI (AtomS3) and Serial MIDI (M5Core) devices
"""

import dearpygui.dearpygui as dpg
import rtmidi
import serial
import serial.tools.list_ports
from typing import Optional
import threading
import time

# MIDI Configuration
MIDI_CHANNEL = 0  # Channel 1 (0-indexed)

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
NOTE_SCENE_1 = 36
NOTE_BLACKOUT = 48

# Animation mode names
ANIMATION_MODES = [
    "Solid",
    "Dual Solid",
    "Chase",
    "Dash",
    "Waveform",
    "Pulse",
    "Rainbow",
    "Sparkle",
    "Custom 1",
    "Custom 2"
]

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
    ("Forward", 12),
    ("Backward", 38),
    ("Ping Pong", 63),
    ("Random", 88)
]


class LeslieLEDsController:
    def __init__(self):
        self.midi_out: Optional[rtmidi.MidiOut] = None
        self.midi_in: Optional[rtmidi.MidiIn] = None
        self.midi_port_name: Optional[str] = None
        self.serial_port: Optional[serial.Serial] = None
        self.scene_save_mode = False
        self.virtual_port_thread = None
        self.running = False
        self.is_serial_mode = False
        
    def setup_midi(self):
        """Initialize MIDI output and virtual input"""
        self.midi_out = rtmidi.MidiOut()
        
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
                
                # Parse CC messages to update GUI sliders
                if len(midi_message) == 3 and midi_message[0] == 0xB0 + MIDI_CHANNEL:
                    cc_number = midi_message[1]
                    cc_value = midi_message[2]
                    # Update the GUI slider for this CC
                    slider_id = f"cc_{cc_number}_slider"
                    if dpg.does_item_exist(slider_id):
                        dpg.set_value(slider_id, cc_value)
                
                # Forward to Serial MIDI if in serial mode, otherwise USB MIDI
                if self.is_serial_mode:
                    if self.serial_port and self.serial_port.is_open:
                        self._send_serial_midi(midi_message)
                else:
                    if self.midi_out and self.midi_out.is_port_open():
                        self.midi_out.send_message(midi_message)
            time.sleep(0.001)  # Small delay to prevent CPU spinning
        
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
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
            self.serial_port = None
        
        if port_type == "MIDI":
            # Connect to USB MIDI port
            if self.midi_out:
                midi_ports = self.midi_out.get_ports()
                if port_identifier in midi_ports:
                    port_index = midi_ports.index(port_identifier)
                    self.midi_out.open_port(port_index)
                    self.midi_port_name = port_identifier
                    self.is_serial_mode = False
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
    
    # Auto-select and connect to LeslieLEDs device if found
    for label in port_labels:
        if "LeslieLEDs" in label or "M5Stack" in label:
            dpg.set_value("port_combo", label)
            port_type, port_id = port_map[label]
            if controller.connect_port(port_type, port_id):
                mode_text = "Serial MIDI" if port_type == "SERIAL" else "USB MIDI"
                dpg.set_value("status_text", f"Connected ({mode_text}): {label}")
                dpg.configure_item("status_text", color=(100, 255, 100))
            return
    
    # If no LeslieLEDs found, try to find "USB Single Serial" port
    for label in port_labels:
        if "USB Single Serial" in label:
            dpg.set_value("port_combo", label)
            port_type, port_id = port_map[label]
            if controller.connect_port(port_type, port_id):
                mode_text = "Serial MIDI" if port_type == "SERIAL" else "USB MIDI"
                dpg.set_value("status_text", f"Connected ({mode_text}): {label}")
                dpg.configure_item("status_text", color=(100, 255, 100))
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
    mode_index = ANIMATION_MODES.index(app_data)
    # Map to CC value ranges (0-9 for mode 0, 10-19 for mode 1, etc.)
    cc_value = mode_index * 10 + 5  # Middle of range
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
    scene_note = NOTE_SCENE_1 + user_data
    controller.send_note(scene_note)


def on_blackout_button():
    """Handle blackout button"""
    controller.send_note(NOTE_BLACKOUT)


def toggle_scene_save_mode(sender, app_data):
    """Toggle scene save mode"""
    controller.scene_save_mode = app_data
    value = 64 if app_data else 0  # >37 = save mode, 0 = load mode
    controller.send_cc(CC_SCENE_SAVE_MODE, value)


def create_gui():
    """Create the DearPyGUI interface"""
    dpg.create_context()
    
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
            dpg.add_combo(ANIMATION_MODES, default_value=ANIMATION_MODES[0], 
                         callback=on_animation_mode, width=200)
            
            dpg.add_spacer(height=5)
            dpg.add_text("Master Brightness:")
            dpg.add_slider_int(label="##brightness", tag="cc_1_slider", default_value=128, min_value=0, max_value=255,
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
            dpg.add_combo([name for name, _ in MIRROR_MODES], default_value=MIRROR_MODES[0][0],
                         callback=on_mirror_mode, width=200)
            
            dpg.add_text("Direction:")
            dpg.add_combo([name for name, _ in DIRECTION_MODES], default_value=DIRECTION_MODES[0][0],
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
                           callback=toggle_scene_save_mode)
            
            dpg.add_spacer(height=5)
            dpg.add_text("Scene Buttons:")
            
            # Scene buttons in two rows
            with dpg.group(horizontal=True):
                for i in range(5):
                    dpg.add_button(label=f"Scene {i+1}", 
                                 callback=on_scene_button, 
                                 user_data=i,
                                 width=70)
            
            with dpg.group(horizontal=True):
                for i in range(5, 10):
                    dpg.add_button(label=f"Scene {i+1}", 
                                 callback=on_scene_button, 
                                 user_data=i,
                                 width=70)
            
            dpg.add_spacer(height=10)
            dpg.add_button(label="BLACKOUT", callback=on_blackout_button, 
                          width=200, height=40)
    
    dpg.create_viewport(title="LeslieLEDs Controller", width=500, height=900)
    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("main_window", True)


def main():
    """Main entry point"""
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


if __name__ == "__main__":
    main()
