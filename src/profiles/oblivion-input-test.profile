; Diagnostic profile: same prefix and pad mapping as Oblivion, small Win32 input viewer.
[application]
id = oblivion-input-test
name = Oblivion Wine Input Path Test
executable = C:\Games\OblivionGOG\input-directinput64-viewer.exe
working_directory = C:\Games\OblivionGOG
prefix = oblivion-gog
runtime = wine-wow64
architecture = pe64
graphics = dxvk

[display]
desktop = 1920x1080
view = window

[input]
mode = keyboard
mouse = right_stick
mouse_speed = 1200
cross = mouse_left
circle = tab
square = f
triangle = e
l1 = alt
r1 = mouse_left
l2 = z
r2 = c
l3 = ctrl
r3 = r
up = up
down = down
left = left
right = right
options = escape
create = t

[debug]
winedebug = +ps5drv
