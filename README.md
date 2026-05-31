# Security Surveillance System

ESP32-CAM based security surveillance system with PIR motion detection, Telegram photo alerts, and Blynk IoT dashboard updates.

## Overview

This project uses an AI Thinker ESP32-CAM module and a PIR motion sensor to capture images only when motion is detected. When valid motion is detected, the ESP32-CAM captures a JPEG photo, sends it to a Telegram chat through a bot, and updates Blynk virtual pins with the latest capture and stream URLs.

The repository contains a sanitized Arduino sketch. Live Wi-Fi passwords, Blynk tokens, and Telegram bot credentials are intentionally not committed.

## Features

- PIR-triggered motion detection on GPIO13
- ESP32-CAM JPEG image capture using the OV2640 camera
- Telegram Bot API photo upload over HTTPS
- Blynk IoT dashboard integration
- Manual photo capture through a Blynk button
- Local `/capture` endpoint on port 80
- Local MJPEG `/stream` endpoint on port 81
- Simple camera-frame heuristic to reduce false motion alerts

## Hardware

- AI Thinker ESP32-CAM module
- OV2640 camera module
- PIR motion sensor
- FTDI USB-to-serial programmer
- 5V power supply
- Jumper wires or breadboard

## Pin Connections

| Component | ESP32-CAM Pin |
| --- | --- |
| PIR OUT | GPIO13 |
| PIR VCC | 5V |
| PIR GND | GND |
| Flash LED | GPIO4, onboard |
| FTDI TX | U0R |
| FTDI RX | U0T |
| FTDI 5V | 5V |
| FTDI GND | GND |
| Flash mode | GPIO0 to GND while uploading |

## Repository Structure

```text
.
|-- src/security_surveillance/
|   |-- security_surveillance.ino
|   |-- app_httpd.cpp
|   |-- camera_pins.h
|   `-- secrets.example.h
|-- docs/
|   `-- SETUP.md
|-- Group21 Security survillence system.docx
|-- Security-Surveillance-System.pptx
`-- README.md
```

## Code File

Easy-to-find single-file Arduino sketch:

```text
SECURITY_SURVEILLANCE_CODE.ino
```

Modular Arduino sketch:

```text
src/security_surveillance/security_surveillance.ino
```

Supporting files:

- `src/security_surveillance/app_httpd.cpp` starts the capture and stream web endpoints.
- `src/security_surveillance/camera_pins.h` defines the AI Thinker ESP32-CAM pin map.
- `src/security_surveillance/secrets.example.h` shows the required private configuration values.

## Setup

1. Install Arduino IDE.
2. Install ESP32 board support through Boards Manager.
3. Install the Blynk library.
4. Open `src/security_surveillance/security_surveillance.ino`.
5. Copy `src/security_surveillance/secrets.example.h` to `src/security_surveillance/secrets.h`.
6. Fill in Wi-Fi, Blynk, Telegram bot token, and Telegram chat ID values in `secrets.h`.
7. Select the AI Thinker ESP32-CAM board.
8. Connect GPIO0 to GND, upload the sketch, then disconnect GPIO0 from GND and reset the board.

Detailed setup notes are in [docs/SETUP.md](docs/SETUP.md).

## Output and Images

Working output screenshots, circuit diagrams, and expected serial monitor output are documented in [docs/OUTPUT.md](docs/OUTPUT.md).

## Blynk Dashboard

Recommended virtual pins:

| Virtual Pin | Purpose |
| --- | --- |
| V0 | Stream URL |
| V1 | Manual capture button |
| V2 | Latest capture URL |

Create a Blynk event named `motion_detected` if you want Blynk app notifications when motion is confirmed.

## Security Notes

- Do not commit `secrets.h`.
- Rotate any Blynk or Telegram tokens that were previously stored in plain text.
- Use a dedicated Wi-Fi network or strong router firewall rules for deployed cameras.
- Local stream and capture URLs are normally available only on the same Wi-Fi network.

## Project Documents

The original project report and presentation are included as reference material:

- `Group21 Security survillence system.docx`
- `Security-Surveillance-System.pptx`
