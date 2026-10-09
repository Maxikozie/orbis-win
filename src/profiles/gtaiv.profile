; GTA IV Complete Edition, PS4 bring-up profile.
; Launch options and display extent follow upstream gtaiv.profile:
; https://github.com/mpereiraesaa/prospero-win-profiles/blob/main/profiles/gtaiv.profile
; Keep only d3d9 native: this installed copy uses 1911.dll, which loads
; Wine D3D12/DirectComposition and needs Wine DXGI private interfaces.
; The installed DXVK is 3.1.1; upstream documents 2.6.2.
; Use the translator confirmed by the working Pinball baseline.

[application]
id = gtaiv
name = Grand Theft Auto IV
executable = C:\Games\GTAIV\GTAIV.exe
working_directory = C:\Games\GTAIV
arguments = -availablevidmem 1536 -nomemrestrict -norestrictions -width 1920 -height 1080 -refreshrate 60 -fullscreen -rglLanguage en-US
dll_overrides = d3d9=n
prefix = gtaiv
runtime = wine-wow64
architecture = pe32
graphics = dxvk
[debug]
; The crash is fixed and the graphics stack is answered, so neither needs
; thousands of lines a second any more. Controls are the open question, and
; the game loaded three different input DLLs -- xinput9_1_0 (which forwards
; to xinput1_4, verified in its main.c), xinput1_4 itself, and DINPUT8 --
; so the first thing to establish is which one it actually polls.
;
; +xinput: does XInputGetState get called, and does it see a connected pad?
; +dinput: GTA IV enumerates DirectInput too; if it is driving from there,
;   feeding the XInput slot can never work.
; +hid: what DINPUT8 finds underneath.
winedebug = +tid,err+all,+loaddll,+xinput,+dinput,+hid
[display]
desktop = 1920x1080
[input]
mode = xinput

[runtime]
cpu = translator
