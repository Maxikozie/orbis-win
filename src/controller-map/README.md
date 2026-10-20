# Gladius Controller Map

`GLDS10048` is a standalone PS4 input diagnostic. It opens the DualShock 4
through the same `pw_pad_ps5` core and PS4 platform adapter used by the Wine
launcher, then displays the live controller state and the mapped keyboard or
mouse action. It does not start Wine or a game, which isolates pad reads and
mapping from Wine's input queues and title-specific behavior.

The initial map matches `oblivion-gog.profile`: Cross and R1 are mouse clicks;
Circle is Tab; Square is F; Triangle is E; L1 is Alt; L2 is Z; R2 is C; L3 is
Ctrl; R3 is R; D-pad directions are arrows; Options is Escape; Create is T.
Pressed controls and their assigned on-screen keys or mouse button light up.
The status area shows connection state and the pad-open result.

Press Options to enter or leave map-edit mode. In edit mode, D-pad Up/Down
selects a controller button and Left/Right cycles its assigned action through
mouse click, common keys, arrows, and A-Z. The selected controller button and
its current assignment appear at the bottom. Changes are in memory for this
run; they are not written back to the game profile yet. Map changes are also
sent to klog. This makes the app useful for collecting a working mapping
without repeatedly launching Oblivion.

The touchpad is not currently shown or mappable. Oblivion's profile does not
use it, so this does not affect the current test.

Build and package with `scripts/build.sh controller-map --pkg`; upload with
`scripts/deploy.sh controller-map`. Package output is
`pkgout/controller-map/IV0000-GLDS10048_00-GLDS100480000000.pkg`.
