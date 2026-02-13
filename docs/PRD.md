# PRD — Motorized Injector Display (ESP32 HMI)

**Version:** 1.1  
**Date:** 2026-02-12  
**Owner:** Andy / Codex  
**Status:** Draft (approved direction; pending implementation)

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
- State name (wrap allowed; auto-height)
- Up to **two state action buttons** below state
- Navigation to Mould Settings & Common Settings

### 5.2 Mould Selection
- Scrollable list of local mould profiles
- Buttons: **Back**, **Send**, **Edit**, **New**
- **Send & Edit disabled until a mould is selected**
- **Double‑tap OR Edit button** opens Mould Edit

### 5.3 Mould Edit
- Editable fields
- Buttons: **Save**, **Delete**, **Back**
- Back without Save → overlay **Save** / **Cancel** (Cancel discards edits)

### 5.4 Common Settings
- On entry: **QUERY_COMMON** from controller
- Editable list of common params
- **Send disabled until change**
- Back with changes → overlay **Send** / **Cancel**

---

## 6. Storage
- **Mould profiles:** Local storage (SD preferred, flash fallback)
- **Common params:** Remote-only (controller is source of truth)

### SD Toggle & File Manager
- If SD present: show **SD toggle button**
- Minimal file manager: copy/move/delete between SD and flash

---

## 7. Refill Blocks (Plunger Graphic)
- Display-side only
- New block added after Refill
- Block size derived from encoder position at end of first Compression
- Blocks shift downward with each injection
- Block color shifts toward “melted” over time

---

## 8. Data Model

### Display
- `mould_profiles[]` (local)
- `common_params` (transient view from controller)
- `plunger_position_cm3`
- `machine_state`

### Controller
- Holds 1 active mould profile
- Receives COMMON updates on Send

---

## 9. Implementation Notes
- LVGL v9.3 (update from v8.3 in legacy code)
- Keep error frame on all screens
- State buttons only appear in allowed states
- Position unit conversion to cm³ shown in UI

---

## 10. Open Questions
None (PRD updated per 2026‑02‑12 answers)

