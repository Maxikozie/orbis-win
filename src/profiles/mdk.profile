; MDK Direct3D version, staged from $GAME_SOURCE/games/MDK Final/SHINY/MDK.
[application]
id = mdk
name = MDK
executable = C:\SHINY\MDK\MDKD3D.EXE
working_directory = C:\SHINY\MDK
dll_overrides = d3d9=n;dxgi=b
prefix = mdk
runtime = wine-wow64
architecture = pe32
graphics = dxvk

[display]
desktop = 1920x1080
view = window

[input]
; Keep XInput for MDK's right-stick movement. Leave Cross unbound on both
; paths while isolating its lag; keyboard Enter may trigger a costly action.
mode = xinput
mouse = none
cross = none
circle = none
square = none
triangle = none
up = up
down = down
left = left
right = right
l1 = alt
r1 = ctrl
l2 = lbracket
r2 = rbracket
options = f12

[runtime]
; The current package lacks wow64native.dll, so a native request falls back
; to the prefix's translator. Keep the selected backend explicit until built.
cpu = translator

[debug]
winedebug = +fps
