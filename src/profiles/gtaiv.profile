; GTA IV Complete Edition, PS4 bring-up profile.
; Launch options and display extent follow upstream gtaiv.profile:
; https://github.com/mpereiraesaa/prospero-win-profiles/blob/main/profiles/gtaiv.profile
; Keep d3d9 native but force dxgi builtin: this copy has app-local DXVK
; dxgi.dll, which otherwise auto-loads native and cannot answer the Wine
; private DXGI interface requested by 1911.dll's Wine D3D12 path.
; The installed DXVK is 3.1.1; upstream documents 2.6.2.

[application]
id = gtaiv
name = Grand Theft Auto IV
executable = C:\Games\GTAIV\GTAIV.exe
working_directory = C:\Games\GTAIV
arguments = -availablevidmem 1536 -nomemrestrict -norestrictions -width 1920 -height 1080 -refreshrate 60 -fullscreen -rglLanguage en-US
dll_overrides = d3d9=n;dxgi=b
prefix = gtaiv
runtime = wine-wow64
architecture = pe32
graphics = dxvk
[debug]
; Keep errors and module-load records; the DXGI and WinMM trace channels emit
; per-frame traffic and obscure the failure with excessive logging.
winedebug = +tid,err+all,+loaddll
[display]
desktop = 1920x1080
[input]
mode = xinput
