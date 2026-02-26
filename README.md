# Smart Canteen Management System

An ESP01 + Arduino based smart canteen system featuring RFID-based access, an interactive keypad/LCD menu, cart management, and a live web dashboard for the kitchen.

---

## Features

| Layer | What it does |
|---|---|
| **Arduino** | Reads RFID cards, drives 16×2 LCD + 4×4 keypad, controls buzzer and two LEDs, communicates with ESP01 over Serial |
| **ESP01 (ESP8266)** | Hosts a WiFi web server, exposes a REST API, persists users & menu in LittleFS |
| **Web Dashboard** | Users tab · Menu tab · Kitchen Panel with live order polling and audio callout |

---

## Hardware

### Components
| Component | Quantity |
|---|---|
| Arduino Uno / Nano | 1 |
| ESP-01 (ESP8266, ≥ 1 MB flash) | 1 |
| MFRC522 RFID reader + cards/tags | 1 |
| LCD 16×2 with I²C backpack (addr 0x27) | 1 |
| 4×4 Matrix Keypad | 1 |
| Passive Buzzer | 1 |
| Green LED + 220 Ω resistor | 1 |
| Red LED + 220 Ω resistor | 1 |
| 3.3 V regulator or voltage divider (for ESP01 RX) | 1 |

### Wiring — Arduino ↔ Peripherals

```
MFRC522  →  Arduino
  SDA/SS    pin 10
  RST       pin  9
  MOSI      pin 11
  MISO      pin 12
  SCK       pin 13
  VCC       3.3 V
  GND       GND

LCD I²C  →  Arduino
  SDA       A4
  SCL       A5
  VCC       5 V
  GND       GND

4×4 Keypad  →  Arduino
  Row 1-4    pins 4, 5, 6, 7
  Col 1-4    pins A0, A1, A2, A3

Buzzer     →  pin 8 / GND
Green LED  →  pin 2 (via 220 Ω) / GND
Red LED    →  pin 3 (via 220 Ω) / GND
```

### Wiring — Arduino ↔ ESP01 (Serial bridge)

```
Arduino TX (pin 1) ──────────────────── ESP01 RX (GPIO3)
Arduino RX (pin 0) ──[1k/2k divider]── ESP01 TX (GPIO1)
ESP01 VCC / CH_PD  ──────────────────── 3.3 V
ESP01 GND          ──────────────────── GND
```

> **Important:** Disconnect the ESP01 from pin 0/1 **before** uploading a sketch via USB, then reconnect.  
> The voltage divider on the RX line protects the ESP01 (5 V Arduino TX → 3.3 V ESP01 RX).

---

## Software Setup

### 1 — Arduino IDE Board Support

Add the ESP8266 board manager URL:  
`https://arduino.esp8266.com/stable/package_esp8266com_index.json`  
Then install **esp8266** by ESP8266 Community.

### 2 — Required Libraries (install via Library Manager)

| Library | Used by |
|---|---|
| MFRC522 by GithubCommunity | Arduino sketch |
| LiquidCrystal I2C by Frank de Brabander | Arduino sketch |
| Keypad by Mark Stanley | Arduino sketch |
| ArduinoJson v6 by Benoit Blanchon | ESP01 sketch |

### 3 — Configure WiFi credentials

Open `esp01/smart_canteen_esp01/smart_canteen_esp01.ino` and update:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

### 4 — Upload ESP01 sketch

1. Select board **Generic ESP8266 Module** (or AI Thinker ESP-01).
2. Set Flash Size to **1MB (FS: 512KB, OTA: ~246KB)** — adjust to your module.
3. Upload `esp01/smart_canteen_esp01/smart_canteen_esp01.ino`.

### 5 — Upload web files to LittleFS

1. Install the [Arduino LittleFS upload tool](https://github.com/earlephilhower/arduino-littlefs-upload).
2. Place `esp01/smart_canteen_esp01/data/index.html` inside the sketch's `data/` folder.
3. Use **Tools → LittleFS Data Upload** (or the plugin shortcut) to flash the filesystem.

### 6 — Upload Arduino sketch

1. **Disconnect the ESP01** from pins 0 and 1.
2. Select **Arduino Uno** (or your board).
3. Upload `arduino/smart_canteen_arduino/smart_canteen_arduino.ino`.
4. Reconnect the ESP01.

---

## Operating the System

### Arduino / Keypad Controls

| Key | Context | Action |
|---|---|---|
| **A** | Menu browse / Cart view | Next item |
| **B** | Menu browse / Cart view | Previous item |
| **C** | Menu browse | Select item → enter quantity |
| **C** | Quantity entry | Confirm quantity → add to cart |
| **#** | Quantity entry | Backspace |
| **D** | Quantity entry | Cancel → back to menu |
| **1** | Menu browse | View cart |
| **D** | Cart view | Exit cart → back to menu |
| **\*** | Menu browse | Place order (deducts balance) |
| **D** | Menu browse | Logout |

### Flow

```
Power On ─► "SMART CANTEEN / MANAGEMENT SYS" splash
         ─► "Scan Your Card"

Scan card ─► ESP01 verifies UID
           ├─ DENIED  ─► Red LED + long buzz ─► back to idle
           └─ GRANTED ─► Green LED + short buzz ─► menu loads

Menu ─► browse with A/B ─► C to select ─► enter qty ─► C to add to cart
     ─► press * to place order ─► balance deducted ─► order sent to kitchen

Kitchen Panel (browser) ─► see live orders ─► click "Ready"
  ─► browser plays chime  AND  Arduino buzzes + LCD shows "Order Ready!"
  OR ─► click "Cancel" ─► balance refunded, stock restored
```

---

## Web Dashboard

After the ESP01 connects to WiFi, open a browser and navigate to the IP address printed by the ESP01 (you can find it in your router's DHCP table or by watching the ESP01 boot with a 3.3 V FTDI adapter).

| Tab | Purpose |
|---|---|
| **Users** | Add users with UID / name / initial balance; delete users |
| **Menu** | Add menu items with name / price / quantity; delete items |
| **Kitchen Panel** | Live view (auto-refresh every 3 s) of pending orders; Ready / Cancel buttons; audio chime on Ready |

---

## Project Structure

```
├── arduino/
│   └── smart_canteen_arduino/
│       └── smart_canteen_arduino.ino   ← Upload to Arduino
└── esp01/
    └── smart_canteen_esp01/
        ├── smart_canteen_esp01.ino     ← Upload to ESP01
        └── data/
            └── index.html              ← Upload via LittleFS tool
```
