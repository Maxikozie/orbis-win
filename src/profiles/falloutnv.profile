; Fallout: New Vegas Ultimate Edition, staged from the installed PC copy.
; Directly launch the game executable; do not run the Windows launcher or
; bundled redistributables on the console.
; The installed game does not include DXVK. The launch folder carries the
; console-tested 32-bit DXVK d3d9.dll copied from the GTA IV prefix.
[application]
id = falloutnv
name = Fallout: New Vegas Ultimate Edition
executable = C:\Games\FalloutNV\FalloutNV.exe
working_directory = C:\Games\FalloutNV
dll_overrides = d3d9=n;dxgi=b
prefix = falloutnv
runtime = wine-wow64
architecture = pe32
graphics = dxvk

[display]
desktop = 1920x1080
view = desktop

[input]
mode = xinput

[runtime]
cpu = native

[debug]
winedebug = +tid,err+all,+loaddll,+virtual
