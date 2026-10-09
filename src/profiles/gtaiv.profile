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
winedebug = +tid,err+all,+loaddll,+vulkan,+d3d12
[display]
desktop = 1920x1080
[input]
mode = xinput

[runtime]
cpu = translator
