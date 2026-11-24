# Game Mode Toggle: install and submit

## Put it in Windhawk's editor

Windhawk's `C:\ProgramData\Windhawk\EditorWorkspace` folder is read-only for normal programs, so the file is
pasted into the editor instead of copied.

1. Copy the whole file to the clipboard:
   `Get-Content C:\Users\<you>\GameMode\windhawk\game-mode-toggle.wh.cpp -Raw | Set-Clipboard`
2. In Windhawk: **Create a new mod**, click into the editor, **Ctrl+A**, **Ctrl+V**.
3. **Ctrl+B** to compile, then **Enable mod** (the left panel's "Compile Mod" then the enable switch).
4. Open **Settings**, add your launchers under *Apps to keep* and your game folders, then press the hotkey.

The mod runs in its own small `windhawk.exe -tool-mod "game-mode-toggle"` process. It does nothing until the
hotkey is pressed.

## Before the first real use

- Do not test it from inside an app that would be closed (a Claude, browser or editor session): that app is
  closed. Put such apps under *Apps to keep* first.
- With "Ask before turning on and off" left on, the confirmation lists exactly what will be closed.

## Submit to the public catalog (optional)

1. Fork `ramensoftware/windhawk-mods`.
2. Add the file as `mods/game-mode-toggle.wh.cpp` (the file name must equal the `@id`).
3. Open a pull request. Windhawk's automation checks the metadata, the readme and that it compiles; a
   maintainer reviews it.
4. Screenshots are optional. Two of the confirmation dialog are in `screenshots\` (`confirm-on.png` and
   `confirm-off.png`, made with sample app names). The readme can only show images by URL, so upload them
   with the pull request or to the repo and link them in the readme, for example
   `![Turning on](https://raw.githubusercontent.com/<you>/<repo>/main/confirm-on.png)`. The folder is
   git-ignored and meant to be deleted once they are uploaded. To recreate them:
   `powershell -File test\capture-dialog.ps1 -Exe test\out\shot32.exe -OutDir screenshots`
5. Every later change needs a higher `@version`.

Things a reviewer is likely to ask about, and the answers:

- *Why a tool mod?* It hooks nothing and injects nowhere; it only needs a hotkey and the Win32 API, which is
  what the "Mods as tools" pattern is for.
- *Can it lose data?* It sends a normal close request. Nothing is force-closed unless the user lists the app
  for it, and an app that asks to save simply stays open.
- *Why are services not handled?* They need administrator rights, which a normal-privilege tool must not have.

## Tests

Windhawk compiles every mod for 32-bit and 64-bit, and `windhawk.exe` itself is a 32-bit program, so the mod
really runs as a 32-bit process on 64-bit Windows. Build and run the tests for both:
`clang++ -target i686-w64-mingw32` and `-target x86_64-w64-mingw32`, with the flags from
`C:\ProgramData\Windhawk\EditorWorkspace\compile_flags.txt` plus `-DWH_EDITING -municode -static`.

`test\test.cpp` builds with Windhawk's own compiler (see its header comment) and has three modes:
`unit` (pure logic), `probe` (read-only look at the PC) and `live <tests dir>` (turn on and off against the fake
apps built by `..\tests\build-fakes.ps1`; it refuses to run if a real app could be reached).
