# Mailbox Notifier

An ultra-low power IoT device built with ESP32-C3 and C++ (ESP-IDF with Arduino component) that notifies users via email when physical mail arrives in their mailbox.

## Description

This project is a hardware and software solution designed to detect the presence of physical mail and send email notifications. It uses a Time-of-Flight laser sensor to measure the distance inside the mailbox and verify if there's new letter. 

To maximize battery life, the system implements **Ultra-Low Power** techniques. Instead of relying on the microcontroller's standard deep sleep, the project uses an external M5Stamp Timer Power module to perform **Power Latching**. The ESP32 remains completely disconnected from power until awoken by a physical interrupt (opening the mailbox flap via a reed switch) or a scheduled RTC alarm. 

### Technical Features
* **Hardware Power Latching:** The MCU self-latches power via `GPIO_NUM_3` upon wake-up and completely cuts its own power supply once execution is finished.
* **Smart Service Mode:** Features a dynamic fallback Access Point (AP). If the device detects a predefined "Service" Wi-Fi network from a smartphone, it hosts an internal Web Server, allowing the user to configure settings (Wi-Fi, SMTP credentials, mailbox depth, wake-up intervals) via a web browser without flashing new code.
* **Battery Monitoring:** Measures battery voltage via ADC to append power status to email notifications.

## Hardware Components

* **Microcontroller:** Seeed Studio XIAO ESP32-C3
* **Distance Sensor:** Adafruit VL53L0X 
* **Power Management:** M5Stamp Timer Power (+RTC)
* **Trigger:** Magnetic Reed Switch (Normally Closed)
* **Power Source:** Li-Pol Battery

## Project Structure

```text
.
├── schematics/
│   └── mailbox_schematic.sch  # EAGLE Schematic file
│   └── mailbox_schematic.png  # Schematic image
├── src/
│   └── main.cpp          # Application logic
```

## Installation & Build

### Prerequisites
* **Visual Studio Code** with the **ESP-IDF** extension installed.
* Arduino component enabled within the ESP-IDF framework.
* Required libraries:
  * `Adafruit_VL53L0X`
  * `ESP_Mail_Client`
  *	`I2C_BM8563_RTC`

### Installing & Flashing
1. **Clone the repository:**
```bash
git clone https://github.com/KBieszczad/Mailbox-Notifier.git
```
2. **Open the project** in VS Code.
3. **Connect your XIAO ESP32-C3** via USB-C.
4. **Build and Flash** the firmware using the ESP-IDF build tools.

## Usage & Configuration

1. **Initial Setup (Service Mode):**
   * Turn on a Wi-Fi Hotspot on your phone named `SERWIS`.
   * Wake up the device (trigger the reed switch). The ESP32 will detect the hotspot and enter AP Mode, broadcasting a network named `MailboxSetup`.
   * Connect to `MailboxSetup` (Password: `12345678`) and navigate to `http://192.168.4.1` in your browser.
2. **Web Configuration:**
   * Enter your home Wi-Fi credentials.
   * Provide the sender, recipient email addresses and gmail app password.
   * Calibrate the remaining settings.
   * Click **Save and Reset**. The device will save credentials to NVS and reboot into standard operation mode.
3. **Standard Operation:**
   * When mail is dropped, the reed switch triggers the system. The ToF sensor measures distance, and if an object is detected, an email is dispatched. Power is immediately cut afterward to save battery.

## Authors

Krzysztof Bieszczad
[@KBieszczad](https://github.com/KBieszczad)
