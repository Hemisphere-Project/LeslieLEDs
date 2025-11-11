#!/usr/bin/env python3
"""
LeslieLEDs MIDI Controller
Simple DearPyGUI interface for controlling LeslieLEDs without a DAW
"""

import dearpygui.dearpygui as dpg
import rtmidi
from typing import Optional

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
        self.midi_port_name: Optional[str] = None
        self.scene_save_mode = False
        
    def setup_midi(self):
        """Initialize MIDI output"""
        self.midi_out = rtmidi.MidiOut()
        
    def get_available_ports(self):
        """Get list of available MIDI output ports"""
        if not self.midi_out:
            return []
        return self.midi_out.get_ports()
    
    def connect_port(self, port_index: int):
        """Connect to a MIDI port"""
        if self.midi_out:
            if self.midi_out.is_port_open():
                self.midi_out.close_port()
            self.midi_out.open_port(port_index)
            self.midi_port_name = self.midi_out.get_ports()[port_index]
            return True
        return False
    
    def send_cc(self, cc_number: int, value: int):
        """Send MIDI CC message"""
        if self.midi_out and self.midi_out.is_port_open():
            # Clamp value to 0-127
            value = max(0, min(127, int(value)))
            message = [0xB0 + MIDI_CHANNEL, cc_number, value]
            self.midi_out.send_message(message)
    
    def send_note(self, note: int, velocity: int = 127):
        """Send MIDI note on message"""
        if self.midi_out and self.midi_out.is_port_open():
            message = [0x90 + MIDI_CHANNEL, note, velocity]
            self.midi_out.send_message(message)
            # Send note off after a short time (handled by receiver)
            
    def cleanup(self):
        """Clean up MIDI resources"""
        if self.midi_out and self.midi_out.is_port_open():
            self.midi_out.close_port()


# Global controller instance
controller = LeslieLEDsController()


def refresh_ports():
    """Refresh MIDI port list"""
    ports = controller.get_available_ports()
    dpg.configure_item("port_combo", items=ports)
    
    # Auto-select and connect to LeslieLEDs device if found
    for i, port in enumerate(ports):
        if "LeslieLEDs" in port:
            dpg.set_value("port_combo", port)
            if controller.connect_port(i):
                dpg.set_value("status_text", f"Connected: {port}")
                dpg.configure_item("status_text", color=(100, 255, 100))
            return
    
    # If no LeslieLEDs found, just select first port if available
    if ports:
        dpg.set_value("port_combo", ports[0])


def connect_midi(sender, app_data):
    """Connect to selected MIDI port"""
    port_name = dpg.get_value("port_combo")
    ports = controller.get_available_ports()
    if port_name in ports:
        port_index = ports.index(port_name)
        if controller.connect_port(port_index):
            dpg.set_value("status_text", f"Connected: {port_name}")
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
            dpg.add_text("MIDI Port:")
            dpg.add_combo([], tag="port_combo", width=300)
            dpg.add_button(label="Refresh", callback=refresh_ports)
            dpg.add_button(label="Connect", callback=connect_midi)
        
        dpg.add_text("Not connected", tag="status_text", color=(255, 100, 100))
        dpg.add_separator()
        
        # Global Controls
        with dpg.collapsing_header(label="Global Controls", default_open=True):
            
            dpg.add_text("Animation Mode:")
            dpg.add_combo(ANIMATION_MODES, default_value=ANIMATION_MODES[0], 
                         callback=on_animation_mode, width=200)
            
            dpg.add_spacer(height=5)
            dpg.add_text("Master Brightness:")
            dpg.add_slider_int(label="##brightness", default_value=128, min_value=0, max_value=255,
                              callback=on_cc_slider, user_data=CC_MASTER_BRIGHTNESS, width=300)
            
            dpg.add_text("Animation Speed:")
            dpg.add_slider_int(label="##speed", default_value=64, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_ANIMATION_SPEED, width=300)
            
            dpg.add_text("Animation Control:")
            dpg.add_slider_int(label="##ctrl", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_ANIMATION_CTRL, width=300)
            
            dpg.add_text("Strobe Rate (0=off):")
            dpg.add_slider_int(label="##strobe", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_STROBE_RATE, width=300)
            
            dpg.add_text("Blend Mode:")
            dpg.add_slider_int(label="##blend", default_value=0, min_value=0, max_value=127,
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
            dpg.add_slider_int(label="##a_hue", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_HUE, width=300)
            
            dpg.add_text("Saturation:")
            dpg.add_slider_int(label="##a_sat", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_SATURATION, width=300)
            
            dpg.add_text("Value:")
            dpg.add_slider_int(label="##a_val", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_VALUE, width=300)
            
            dpg.add_text("White:")
            dpg.add_slider_int(label="##a_white", default_value=0, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_A_WHITE, width=300)
        
        dpg.add_separator()
        
        # Color B Controls
        with dpg.collapsing_header(label="Color B (RGBW)", default_open=True):
            dpg.add_text("Hue:")
            dpg.add_slider_int(label="##b_hue", default_value=64, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_B_HUE, width=300)
            
            dpg.add_text("Saturation:")
            dpg.add_slider_int(label="##b_sat", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_B_SATURATION, width=300)
            
            dpg.add_text("Value:")
            dpg.add_slider_int(label="##b_val", default_value=127, min_value=0, max_value=127,
                              callback=on_cc_slider, user_data=CC_COLOR_B_VALUE, width=300)
            
            dpg.add_text("White:")
            dpg.add_slider_int(label="##b_white", default_value=0, min_value=0, max_value=127,
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
