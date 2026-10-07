# archive/ — earlier code pulled from git history for the handover

Not built, not current. Here so the next person can see what was tried without git archaeology.

## gateway_rig_test-history/
Snapshots of `firmware/gateway_rig_test` at the commits that matter (see COMMIT.txt in each):
- 01 — first full-rig build: Advatek-style page with the pixel map (/map.bin), select by run/board, chase/wipe/cycle/board-step/universe-step/ends/rainbow/breathe/radial/spot modes, live WebSocket mirror, channel testers, scenes, OTA.
- 02 — + per-ring dome-shading tuner with the "light the ring being tuned" magenta highlight, config export/import.
- 03 — last version of the full page before it was stripped (heap cut by ~35 kB, page gzipped). The pixel-selection / channel-tester / live-mirror code lives here.
- 04 — `c018073`, the stripped known-good build (Off/Solid/Scene + dome tuner) the later reverts went back to.
- 05 — Pixel-rim scenes + task watchdogs + web-server self-test. Did NOT serve the page on hardware; reverted. The watchdog/self-test ideas are worth salvaging.

The current `firmware/gateway_rig_test` is 04 plus small fixes; `firmware/sun_show` is the production show.

## Superseded pixel maps
`simulator/sun-map.pre-cw.*` (sectors numbered anticlockwise) and `simulator/sun-map.pre-Cflip.*` (C boards chained ascending) are in the main tree. Never load them.
