# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & run

```sh
pebble build                        # compile for all target platforms
pebble install --emulator emery     # run on the Pebble Time 2 emulator
pebble install --phone <ip>         # sideload to a paired phone
pebble logs --emulator emery        # stream APP_LOG output from the emulator
```

The build produces `build/pebble-todo.pbw` (a zip of per-platform `.bin` files). The `wscript` (WAF) globs all `src/c/**/*.c` automatically, so new source files are picked up without editing the build script.

Target platforms are declared in `package.json` under `pebble.targetPlatforms`. Modern hardware is **emery** (Pebble Time 2), **gabbro** (Pebble Round 2), **flint** (Pebble 2). The older platforms (aplite, basalt, chalk, diorite) are included for backwards compatibility.

SDK documentation: <https://developer.repebble.com>

## Architecture

Everything lives in a single file: `src/c/pebble-todo.c`.

**Data model** — a flat array of up to 32 `Task` structs (`text[60]` + `done` bool) held in static globals. Tasks are loaded from Pebble's flash-backed `persist_*` API on init and flushed back on deinit.

**UI** — one `Window` containing a `MenuLayer`. The menu has a single section; row 0 is always the "+ Add task" row; rows 1–N map to `s_tasks[row - 1]`. Done items are rendered with a 2px strikethrough drawn manually in `menu_draw_row`.

**Input flow**:
- Select on row 0 → opens a `DictationSession`; on success the transcription is appended as a new task.
- Select on a task row → opens an `ActionMenu` with "Mark Done / Mark Undone" and "Delete". `s_action_target` holds the target index while the menu is open and is reset in `action_menu_did_close`.

**Color gating** — color-only APIs (highlight colors, `GColorChromeYellow`) are wrapped in `#if defined(PBL_COLOR)` guards to keep aplite/flint builds clean.

**Phone-side JS** — no `src/pkjs/` code exists yet. If added, it is bundled automatically by `wscript` and loaded by the companion app.
