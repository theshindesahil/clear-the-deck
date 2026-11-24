# clear-the-deck

One hotkey to get a Windows gaming laptop ready to play, and one more press to put everything back exactly as it was.

Written for Windows PowerShell 5.1 (the one that ships with Windows 11). No background process, no installer: you press **Ctrl+Alt+G**, a confirmation dialog shows what will change, you confirm.

## What it does

**First press (ON)** takes a fresh snapshot, then:

- switches to the Ultimate Performance power plan (falls back safely if the plan does not exist)
- closes the apps listed in `config.json`, and, with `closeOthers`, every other normal desktop app that is not needed for gaming
- stops the services the elevated helper allows (for example a local PostgreSQL), through an on-demand scheduled task
- raises running game processes (from your game folders) to High priority, never Realtime

**Second press (OFF)** restores exactly that snapshot and deletes it: priorities, services, reopened apps, power plan. There is no stored "original", only the snapshot taken at each ON, so anything you change while not gaming stays changed.

A logon task runs the restore silently if a snapshot is still around after a crash or reboot. It waits for Windows to finish starting its own startup apps first, so nothing is launched twice.

## How apps are closed

- A polite close request (WM_CLOSE) goes to every window of the app, hidden tray windows included, to all apps at the same moment, with one shared wait. Per-app deadline, 8 s by default.
- Listed apps are force-killed after their deadline unless the entry has `"forceKill": false` (browsers, chat apps).
- Apps found by `closeOthers` are never force-killed. One that asks "save changes?" simply stays open and is left alone on OFF.
- Never touched: anything under `C:\Windows`, console tools (git, node, shells), anything in `keepNames` / `keepPaths`, apps started by a game launcher, games, and apps with no window to close.
- Store apps are reopened by AppUserModelID. A link a browser was started with (OAuth pages and the like) is not replayed.

## Install

1. Put this folder somewhere permanent, for example `C:\Users\<you>\GameMode`.
2. Copy `config.example.json` to `config.json` and edit it: your game folders, the apps to close, the processes to keep.
3. Optional, for services: copy `helper\services.ps1` to `C:\Program Files\GameMode\services.ps1` (admin-writable only), edit its `$allow` list, and register two elevated scheduled tasks named `GameMode-StopServices` and `GameMode-StartServices` that run it with `-Action Stop` / `-Action Start`. The toggle reads the service list from that file, so there is a single list.
4. Optional, for crash recovery: a logon task named `GameMode-Recover` running `gamemode.ps1 -Recover -Quiet`.
5. Make a shortcut with the hotkey: target `conhost.exe`, arguments `--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -File "<path>\gamemode.ps1"`, shortcut key Ctrl+Alt+G.

## Command line

| Switch | Meaning |
|---|---|
| `-WhatIf` | print what would happen, change nothing |
| `-Quiet` | skip the dialog |
| `-Recover` | restore only if a snapshot exists (logon task) |
| `-Reset` | set a stuck snapshot aside without restoring anything |
| `-Exclude "A,B"` | leave these `config.json` apps alone this run. One double-quoted list: through `powershell -File`, single-quoted lists are not parsed by PowerShell and are rejected |
| `-PreviewPng <path>` | with `-WhatIf` only: render the dialog to a PNG |

## Safety notes

- Do not run a live toggle from inside an app that is on the close list; it will end that session. The tests use fake apps for this reason.
- `state.json` stores the command line of closed apps. It is deleted on a clean OFF; `.gitignore` keeps it out of the repo.
- The elevated helper can only touch the services in its own allowlist. Its folder must not be writable by normal users.
- Snapshots are written to a temp file and renamed into place; an unreadable snapshot is moved aside and logged instead of blocking the hotkey.

## Tests

Fake apps only, no real app, service or power plan is touched.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\build-fakes.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-extra-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\run-closeothers-test.ps1
```

They cover round trips, `-Exclude` parsing, corrupt and empty snapshots, an ON killed halfway, a failed relaunch, a busy hotkey, dry runs, the services allowlist and the logon wait.

## Files

| File | Purpose |
|---|---|
| `gamemode.ps1` | the toggle |
| `dialog.ps1` | themed WPF confirmation dialog (dark and light, keyboard accessible) |
| `config.example.json` | settings template |
| `helper/services.ps1` | elevated helper with the service allowlist |
| `tests/` | fake-app test harness |

No license has been chosen yet.
