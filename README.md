
# 🎹 MelodiUS — Twin Piano Experience

> Connecting people through synchronised music, anywhere in the world.

## What Is It?
MelodiUS is a pair of IoT-connected mini pianos that synchronise in real time.
When a key is pressed on Piano A, the corresponding key lights up (and optionally
moves via solenoid) on Piano B — regardless of distance.

## How It Works
Key Press → ESP32 detects input → MQTT message sent over WiFi →
Received by Piano B → LED lights up + sound plays + (optional) solenoid activates

## Hardware
- ESP32 microcontroller (x2)
- Push-button / Hall sensor keys
- RGB LEDs (one per key)
- Solenoid actuators (Plan A) or LED+sound only (Plan B)
- 3D printed enclosure (FDM, Fusion 360)

## Software
- Arduino / MicroPython firmware
- MQTT protocol for messaging
- Sound generation via speaker module

## Demo Video
[Link to video here]

## Project Structure
See folder breakdown in /docs/project-brief.md

## Team — Group 6 GPP
- Madina — Project Lead & System Integration
- [Name] — Hardware Development
- [Name] — Software & Connectivity
- [Name] — UX & User Testing

## References
See /docs/references.md
EOF
