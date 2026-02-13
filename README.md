# Motorized Injector Display (ESP32 HMI)

This repo contains the **ESP32 touchscreen UI** for the motorized injector. The UI is designed in **EEZ Studio** and rendered with **LVGL v9.3**.

## Overview
- **Display**: Elecrow 5" HMI ESP32 RGB Touch Screen (480×800)
- **Framework**: LVGL v9.3 + EEZ Flow
- **Input**: Touch only
- **Comms**: UART to the injector controller (ESP32)
- **Purpose**: Show live state/temperature/position, manage mould profiles locally, and send parameter updates to the controller.

## Layout (Always Visible)
- **Left column**: Plunger/barrel graphic
  - **Top label**: Plunger position in **cm³**
  - **Bottom label**: Temperature in **°C**
- **Right 2/3**: Screen-specific content
- **Errors**: Red frame around right 2/3 on all screens; error text shown on Main only.

## Screens
- **Main**: State name + state action buttons; navigation to Mould/Common settings
- **Mould Selection**: Scrollable list; `Send` + `Edit` enabled only when a mould is selected; `New` button
- **Mould Edit**: Editable fields; `Save`, `Delete`, `Back` (Back prompts to Save/Cancel)
- **Common Settings**: Values loaded from controller on entry; edits tracked; `Send` enabled only after changes; Back prompts Save/Cancel

## Storage
- **Mould profiles** are stored **locally** (SD preferred, flash fallback).
- **Common parameters** are **remote-only** (always read from controller, and sent back on Save).
- Optional SD file manager (minimal: copy/move/delete between SD and flash).

## Comms Protocol
The protocol is defined in `docs/DisplayComms_Protocol.md` and will be updated to:
- Send `ENC|position` only (no velocity)
- Support mould/common parameter updates via `MOULD|...` and `COMMON|...`

## Project Structure
```
pp-motorized-injector-ui/
├── include/                 # LVGL + touch config
├── src/
│   ├── main.cpp             # UI entry point
│   └── ui/                  # EEZ-generated LVGL UI
├── injector_initial_layout.eez-project
├── platformio.ini
├── README.md
└── docs/
    └── PRD.md               # Product Requirements Document
```

## Development
- EEZ Studio generates code in `src/ui/`
- Custom behavior lives in `src/main.cpp` and `src/ui/actions_impl.*`

## Next Steps
- Refactor UI code to ESP-IDF in `esp-idf` branch
- Integrate UART protocol with injector controller

