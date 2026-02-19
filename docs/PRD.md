# PRD — Motorized Injector Display (ESP32 HMI)

**Version:** 1.2
**Date:** 2026-02-18
**Owner:** Andy / Codex
**Status:** Implementation Phase

---

## 1. Purpose
Create a touchscreen UI for the motorized injector that:
- Displays **live state, temperature, and plunger position (cm³)**
- Manages **mould profiles locally**
- Manages **common parameters remotely** (always loaded from controller and sent back on save)
- Presents **errors clearly** without blocking browsing or editing

---

## 2. Hardware & Platform
- **Device:** Elecrow 5" HMI ESP32 RGB Touch Screen
- **Resolution:** 480×800
- **Display IC:** ILI6122 & ILI5960
- **Input:** Touch only
- **UI Framework:** LVGL v9.3 + EEZ Flow
- **Connectivity:** UART (Display <-> Controller), USB (Power/Diagnostics)

---

## 3. Communication (UART)
- **Baud:** 115200, 8N1
- **Direction:** Display ↔ Controller ESP32
- **Protocol:** Pipe-delimited SafeString

### Controller → Display
- `ENC|position` (position only, no velocity)
- `STATE|name|timestamp`
- `ERROR|code|message`
- `MOULD_OK|…`
- `COMMON_OK|…`

### Display → Controller
- `MOULD|…` (send on mould selection screen, safe states only)
- `COMMON|…` (send on common settings screen, safe states only)
- `QUERY_MOULD`, `QUERY_COMMON`, `QUERY_STATE`, `QUERY_ERROR`

**Safety:** Display should disable sends outside safe states; controller also rejects unsafe commands.

---

## 4. Global Layout
**Left column (fixed):**
- Plunger/barrel visualization
- **Top label:** position in **cm³**
- **Bottom label:** temperature in **°C**

**Right 2/3:** screen-specific content

**Error indication:**
- Red frame around right 2/3 on **all screens** when error present
- Error text shown **only on Main**

---

## 5. Screens & Flows

### 5.1 Main
- **Current Status**: Implemented. State, Position, Temp labels exist.
- State name (wrap allowed; auto-height)
- Up to **two state action buttons** below state
- Navigation to Mould Settings & Common Settings

### 5.2 Mould Selection
- **Current Status**: UI Implemented, but hangs on "New" due to missing persistence.
- Scrollable list of local mould profiles
- Buttons: **Back**, **Send**, **Edit**, **New**
- **Send & Edit disabled until a mould is selected**
- **Double‑tap OR Edit button** opens Mould Edit

### 5.3 Mould Edit
- **Current Status**: Not Implemented (Buttons exist but show "Pending" notice).
- Adjustable fields for the selected mould profile.
- Buttons: **Save**, **Delete**, **Back**
- Back without Save → overlay **Save** / **Cancel** (Cancel discards edits)

### 5.4 Common Settings
- **Current Status**: UI Implemented. Keyboard logic exists but is not visible/functioning correctly.
- On entry: **QUERY_COMMON** from controller
- Editable list of common params
- **Send disabled until change**
- Back with changes → overlay **Send** / **Cancel**

---

## 6. Storage
- **Mould profiles:** Local storage.
    - **Primary:** LittleFS (Internal Flash) for reliability.
    - **Secondary (Future):** SD Card for export/backup.
- **Common params:** Remote-only (controller is source of truth)

---

## 7. Refill Blocks (Plunger Graphic)
- **Current Status**: Static graphic elements exist; dynamic update logic needed.
- Display-side only
- New block added after Refill
- Block size derived from encoder position at end of first Compression
- Blocks shift downward with each injection
- Block color shifts toward “melted” over time

---

## 8. Data Model

### Display
- `mould_profiles[]` (local, persisted in LittleFS)
- `common_params` (transient view from controller)
- `plunger_position_cm3`
- `machine_state`

### Controller
- Holds 1 active mould profile
- Receives COMMON updates on Send

---

## 9. Implementation Notes
- **Framework:** PlatformIO + Arduino + LVGL 9.3
- **Do not modify working components:** Touch, Display, and Main screen layout are stable.
- **Incremental Changes:** Add features one by one (Persistence -> Keyboard -> Mould Edit -> UART Refinement).

---

## 10. Open Questions
None.

