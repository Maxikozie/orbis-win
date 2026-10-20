; A/B profile for the existing Fallout: New Vegas console prefix.
; Identical to fallout-new-vegas.profile except for the unique profile ID/name
; and explicit translator CPU selection.
[application]
id = fallout-new-vegas-translator
name = Fallout: New Vegas (translator CPU)
executable = C:\Games\FalloutNV\FalloutNV.exe
working_directory = C:\Games\FalloutNV
dll_overrides = d3d8,d3d9,d3d10core,d3d11,dxgi=n
prefix = fallout-new-vegas
runtime = wine-wow64
architecture = pe32
graphics = dxvk

[display]
desktop = 1920x1080
scaling = fit

[input]
mode = xinput

[runtime]
cpu = translator
