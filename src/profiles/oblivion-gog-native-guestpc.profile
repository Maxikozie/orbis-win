; Native WoW64 early-start profile with its built-in per-thread profiler enabled.
[application]
id = oblivion-gog-native-guestpc
name = (Native Guest PC) The Elder Scrolls IV: Oblivion (GOG)
executable = C:\Games\OblivionGOG\Oblivion.exe
working_directory = C:\Games\OblivionGOG
prefix = oblivion-gog
runtime = wine-wow64
architecture = pe32
graphics = dxvk

[display]
desktop = 1920x1080
view = window
show_fps = true

[input]
mode = keyboard
mouse = right_stick
mouse_speed = 1200
keyboard_stick = left_stick
stick_deadzone = 25
cross = space
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
winedebug = -all,+ps5drv,warn+dinput,warn+rawinput,warn+msg,warn+vulkan,+fps,+seh
env = PW_NATIVE_PROFILE=1

[runtime]
cpu = native
