# SMART-CANTEEN-MANAGEMENT-

This repository contains a minimal smart canteen management stack with:

- A lightweight HTTP server that hosts the management dashboard and APIs.
- A browser-based dashboard for user, menu, and kitchen order workflows.
- An Arduino sketch that drives the RFID + keypad + LCD ordering flow.

## Server dashboard

### Requirements

- Node.js 18+

### Run locally

```bash
npm install
npm start
```

The dashboard will be available at `http://localhost:3000`.

### Key API routes

- `POST /api/verify` — Validate a UID and return menu items.
- `POST /api/orders` — Place an order and deduct balance.
- `POST /api/orders/:id/ready` — Mark an order ready (triggers callout in the UI).
- `POST /api/orders/:id/cancel` — Cancel an order and refund balance.

Initial data is stored in `data.json` and can be edited to preload users and menu items.

## Arduino sketch

Upload `arduino/SmartCanteen.ino` to the Arduino board. The sketch expects:

- 16x2 I2C LCD (`0x27`)
- 4x4 matrix keypad
- MFRC522 RFID reader
- ESP-01 connected over Serial for UID verification and order placement
- Two LEDs (access granted/denied) and a buzzer

The Arduino code sends the following line-based commands to the ESP-01:

- `VERIFY,<UID>` — expects a response starting with `ACCESS` to allow ordering.
- `ORDER,<UID>,<menuId>:<qty>|...` — expects a response starting with `OK` when the order is accepted.

Update pin assignments at the top of the sketch if your wiring differs.
