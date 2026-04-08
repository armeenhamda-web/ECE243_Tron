# Tron Game FPGA Implementation
A hardware implementation of the classic Tron light cycle game using Verilog HDL and FPGA technology (DE1-SoC board). This project demonstrates real-time dual-player gameplay, VGA video generation, PS/2 keyboard input processing, collision detection, AI opponent for single-player mode, and audio feedback through dedicated hardware modules.

# Project Overview
This implementation runs entirely on an FPGA without the use of a general-purpose processor, leveraging parallel hardware execution for game logic, rendering, and peripheral communication. The system generates VGA video output at 320×240 resolution with 16-bit color depth (RGB 565), processes PS/2 keyboard input for two players, and supports audio output through an I2S audio codec interface.

# Block Diagram
<img width="1920" height="1080" alt="Tron Final Block Diagram" src="https://github.com/user-attachments/assets/8b11caba-bc32-4a57-a360-3ddcc7dd81e7" />

# Features
| Feature | Description |
|---------|-------------|
| Dual-Player Mode | Two players control light cycles using WASD and IJKL keys |
| Single-Player Mode | Player vs AI with three difficulty levels (Easy/Medium/Hard) |
| Color Selection | Choose between blue or green bike as the player character |
| Dynamic Arena | Different obstacle layouts based on difficulty |
| Trail System | Each bike leaves a trail that acts as a wall |
| Collision Detection | Pixel-perfect collision detection for walls, trails, and opponent |
| Score Tracking | First to 3 points wins |
| Audio Feedback | Collision sounds, game over music, and UI beeps |
| Pause Functionality | Press SPACE to pause/resume |
| Mouse Support | Clickable UI for mode/difficulty/color selection |


## Hardware Architecture

### Target Platform Specifications

| Parameter | Specification |
|-----------|---------------|
| Target FPGA | Intel/Altera Cyclone V (DE1-SoC) |
| System Clock | 50 MHz |
| VGA Resolution | 320 × 240 pixels |
| VGA Refresh Rate | 60 Hz |
| Color Depth | 16 bits per pixel (RGB 565) |
| Pixel Clock | 25 MHz |
| Audio Sample Rate | 8 kHz |
| PS/2 Clock Frequency | ~10-16.7 kHz |

### Memory-Mapped Hardware Addresses

```c
// Hardware base addresses
#define VGA_PIXEL_CTRL_BASE   0xFF203020  // VGA controller
#define VGA_STATUS_OFFSET     0x0C        // VGA status register
#define PS2_BASE_PRIMARY      0xFF200100  // PS/2 keyboard
#define PS2_BASE_MOUSE        0xFF200108  // PS/2 mouse
#define AUDIO_BASE            0xFF203040  // Audio codec
