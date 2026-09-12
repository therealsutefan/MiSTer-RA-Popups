# MiSTer RetroAchievements — Community Build

Community build based on [odelot's MainMiSTer](https://github.com/odelot/Main_MiSTer) v1.12.1 with improved popup presentation and per-core popup positioning.

> **Note:** This is an unofficial community build. The RetroAchievements integration, rcheevos stack, and Game Boy hardcore support are entirely odelot's work. My contributions are the popup improvements and the positioning system. Please do not report bugs to odelot — he is not aware of this binary.

---

## What's included

### From odelot's v1.12.1
- Full RetroAchievements integration via rcheevos
- Game Boy / Game Boy Color hardcore support (save states blocked at core level)
- Async log writer — no more main loop blocking from log I/O
- aplay fix for N64/PSX — fork/exec with timeout, no more zombie processes
- In-game achievement list via Menu+Y shortcut
- Scrolling description ticker in the achievement list
- Subset/DLC completion event
- Global popup position setting (left/center/right)

### My additions
**Achievement popups**
- No "ACHIEVEMENT" header — more space for content
- Word-wrap at word boundaries (title up to 2 lines, description up to 3)
- Points displayed as `[+X]` at the end of the last line
- Display duration scales with point value:
  - < 10 pts → 5 seconds
  - 10–24 pts → 6 seconds
  - 25–49 pts → 7 seconds
  - 50+ pts → 8 seconds

**Challenge popups**
- Compact tags instead of long headers: `[A]` when a challenge starts, `[F]` if you fail it
- If completed successfully, the normal achievement popup handles it — no duplicate popup
- Description starts on line 1, progress (e.g. `3/10`) on the last line

**Progress indicator**
- Title wraps cleanly instead of being hard-cut
- Display duration reduced to 1 second

**Sound fix**
- Achievement sound was playing twice on GAME_COMPLETED — fixed

**Per-core popup positioning**
- Popup position and offset configurable per core via `retroachievements.cfg`
- Useful when popup lands outside the visible play area (e.g. 4:3 content in a 1080p frame)

---

## Setup

1. Back up your existing `/media/fat/MiSTer` binary
2. Copy `MiSTer` to `/media/fat/` (root of the SD card)
3. Fill `retroachievements.cfg` with your RA credentials (`username`, `password`)
4. odelot's modified cores (v1.12.1) are required — get them from [odelot/Main_MiSTer](https://github.com/odelot/Main_MiSTer)

---

## Configuration

Full `retroachievements.cfg` reference:

```ini
# RetroAchievements credentials
username=your_username
password=your_password

# Hardcore mode (1=yes, 0=no) — now also works for GB/GBC
hardcore=1

# Popup visibility (1=yes, 0=no)
show_challenge_show_popup=1    # [A] popup when a challenge starts
show_challenge_hide_popup=1    # [F] popup when a challenge is failed
show_progress_popups=1         # progress update popups
show_progress_name=1           # show achievement name in progress popup
show_leaderboards_updates=1
show_leaderboards_submission=1

# Popup position — global defaults
popup_position=left            # left / center / right
popup_h_offset=0               # horizontal inward offset from edge (steps, 0–511)
popup_v_offset=0               # vertical offset (negative = up)

# Per-core overrides
# Get the exact core name via SSH: grep 'Popup settings' /tmp/ra_debug.log
[NES]
popup_h_offset=80

[SNES]
popup_position=center
popup_h_offset=50

[Gameboy]
popup_h_offset=93

[MegaDrive]
popup_h_offset=75
```

> **Tip:** `popup_h_offset` values between 50–100 are a good starting point for 4:3 games in a 1080p frame. A core reload is sufficient after changing these values — no MiSTer reboot needed.

---

## Credits

Full credit for the RetroAchievements integration: [odelot](https://github.com/odelot/Main_MiSTer)
