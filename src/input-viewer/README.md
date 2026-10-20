# Wine Input Path Viewer

`input_viewer.exe` is a small 32-bit Windows GUI diagnostic for the
`oblivion-gog` Wine prefix. It opens DirectInput 8 keyboard and mouse devices
in exclusive foreground mode, then shows `GetDeviceState` results while also
counting any `WM_INPUT` and legacy `WM_KEY*`/`WM_MOUSE*` messages received by
its window. It uses DirectInput's own Raw Input registration so the diagnostic
does not steal the input target from DirectInput.

Run the `oblivion-input-test` profile from the existing Wine launcher. Its
controller map matches the current Oblivion profile: Cross and R1 produce a
left click, D-pad directions produce arrows, and the remaining buttons produce
their listed keyboard actions. Expected evidence is visible on screen:

- DirectInput keyboard/mouse initialization and polling status.
- Current state for every mapped Oblivion key (Tab, F, E, Alt, Ctrl, Z, C, R,
  T, arrows), plus Space, Enter, and Escape.
- Left/right mouse button state and movement deltas.
- `WM_INPUT` and legacy message counts, if those messages reach the app window.

`WM_KEY*` and `WM_MOUSE*` can remain at zero with DirectInput's exclusive
`RIDEV_NOLEGACY` registration. The important distinction is whether DirectInput
reports the mapped key or mouse button as down. The app is PE32 and can be
rebuilt with:

```sh
i686-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -mwindows \
  -o build/input-viewer.exe src/input-viewer/input_viewer.c \
  -ldinput8 -ldxguid -lgdi32 -luser32
```

The deployed copy is in
`/data/prospero-win/prefixes/oblivion-gog/drive_c/Games/OblivionGOG/` and the
profile is `src/profiles/oblivion-input-test.profile`.

The first console launch crashed about 2.5 seconds after Wine startup, before
we saw any viewer output. The PS4 recorded crash reason `0xb` / exception code
`11`; its crash reporter deleted the coredump before retrieval, so the faulting
instruction is unknown. The current build writes checkpoints to
`input-viewer-stage.log` in the working directory, including immediately before
and after DirectInput creation. Retrieve that file after the next launch to
locate the failing stage. Current executable SHA256:
`fb254b3aa3ff19b064996bfa22ea1c48d29ace72852b5d6ebf6c709ddfe07277`.

The second DirectInput-viewer launch used the verified executable and crashed
with the same kernel reason/exception. The profile was switched to a PE32
`input-basic-viewer.exe` with no DirectInput imports; that also crashed with
the same reason/exception, before its checkpoint appeared.

The PE64 test reached `WinMain` and its message loop, but its first window was
blank because Wine could not select an installed font. The bitmap-font PE64
viewer then rendered correctly. Its screenshot showed 141 mouse moves and a
Win32 `KEYUP vk=0009` (Tab), but `GetAsyncKeyState` reported every watched key
down while no key was held. Runtime logs show mapped key down/up events reaching
`NtUserSendHardwareInput` with status 0. Treat the async-state display as
invalid; ordinary Win32 mouse and keyboard messages are reaching the window.

The current profile now launches `input-directinput64-viewer.exe`, a PE64
DirectInput test with the same bitmap font. It reports keyboard/mouse device
initialization, current DirectInput key state, and buffered event counts. This
is the next test for the API path Oblivion may use. Source is
`directinput_viewer.c`; SHA256:
`64297080a920db0c4b6a0a232a62839525d6f371beb76da6116f1b622efec091`.
