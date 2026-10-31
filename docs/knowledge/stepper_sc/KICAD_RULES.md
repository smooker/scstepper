# KiCad S-Expression Format Rules (learned 2026-03-06)

## Syntax
- **No comments** — `.kicad_sch`/`.kicad_pcb` have NO comment syntax; `;;`, `#`, `//` all cause parse errors
- `wire` only valid at **top-level schematic scope** — NOT inside `lib_symbols`
- Power symbol vertical stems inside `lib_symbols` must use `polyline`, not `wire`
- `no_connect`: `(at X Y)` only — no angle: `(no_connect (at X Y) (uuid "..."))`

## lib_symbols vs symbol instances
- Inside `lib_symbols` shape bodies: only `arc`, `bezier`, `circle`, `pin`, `polyline`, `rectangle`, `text`
- Schematic symbol instances accept only: `lib_id`, `lib_name`, `at`, `mirror`, `uuid`, `on_board`, `in_bom`, `dnp`, `default_instance`, `property`, `pin`, `instances`, `unit`, `exclude_from_sim`
- `pin_names`/`pin_numbers` ONLY valid inside `lib_symbols` — NOT in placed symbol instances

## Power Symbol Layout Principles (stepper_gen.py milestone3)
- **DO NOT use wires to connect power symbols** — place symbol directly at pin endpoint (coincident = connected). KiCad connects by shared endpoint, no wire needed.
- **DO NOT use L-shaped wire routing** — wires crossing other nets cause accidental connections and ERC errors
- **PWR_FLAG co-located** with its power symbol (same x,y) — shared endpoint = ERC satisfied, no wire needed
- **+5V net**: NO PWR_FLAG needed — U1 VO pin is `power_out` and already drives it. Adding PWR_FLAG causes `pin_to_pin: Power output + Power output` ERC error
- **GND, +24V, +3.3V**: need PWR_FLAG (no power_out pin drives them — only passive/power_in pins)
- Column offsets and Y-snap were in milestone3 but caused ERC issues via wire crossings — revisit carefully

## Power Symbol Layout Principles
- Each power net type gets its own **x column** (fixed offset from pin endpoint): no symbol-to-symbol collision
  - `+24V` → pin+5.08, `+5V` → pin+10.16, `GND` → pin+15.24, `+3.3V` → pin+20.32, `PWR_FLAG` → pwr_sym+5.08
- **GND symbols can only go DOWN** (max Y) from their pin — never up
- **Power symbols (+5V, +24V) can only go UP** (min Y) from their pin — never down
- On connectors with +3.3V: snap GND/+5V Y to the +3.3V pin Y (respecting direction rules)
- L-shaped wires for Y-snapped symbols: horizontal from pin to column_x, then vertical to sym_y
- Vertical wire crossings through other power symbols are acceptable in KiCad schematics
- PWR_FLAG must be on the same net node as a power symbol (co-located or wired) for ERC to pass
- `power_pin_not_driven` ERC error = missing PWR_FLAG on that rail

## Mirroring Components (kicad-sch-api)
- `SchematicSymbol` has NO `mirror` field — serializer does NOT write it
- `pin_utils.py` reads `getattr(component, 'mirror', None)` — but setting `_data.mirror` won't persist to file
- **Working method**: post-process the saved `.kicad_sch` file after `sch.save()`
- Find the symbol's placement `(at X Y angle)` (identified by being followed by `(unit N)`)
- Insert `\n\t\t(mirror x)` after it
- Use regex: `r'\(at\s+[\d.]+\s+[\d.]+\s+[\d.]+\)(?=\s*\n\s*\(unit\s)'`
- Find last match before `(property "Reference" "Jn")` marker
- `rotation=180` does NOT mirror — it rotates, flipping pin 1 to bottom — WRONG
- Mirror x keeps pin 1 at top, flips body left↔right ✅

## Current State (milestone5 — ERC CLEAN ✅)
- `stepper_gen.py.milestone4` + `stepper_new.kicad_sch.milestone4` — saved
- Functionality: CORRECT (power symbols outside body, wired, PWR_FLAG offset +X, columns for J11/J12)
- **ERC: 0 errors, 0 warnings** ✅
- Next: PCBnew → export netlist from eeschema → import → place → autoroute

## Autorouting Plan (after ERC clean)
1. eeschema → Inspect → Electrical Rules Checker → run ERC
2. eeschema → File → Export → Netlist (KiCad format) → `stepper.kicad_netlist`
3. Open pcbnew → File → Import → Netlist → import above file
4. All footprints already assigned in `stepper.net`
5. Set board outline (Edge.Cuts layer)
6. Run autorouter (freerouting or KiCad built-in)

## Workflow Notes
- **Use kicad-sch-api** — never hand-roll S-expressions
- Pipeline: `stepper.net` → `netlist_to_schematic.py` → `.kicad_sch` → KiCad ERC → PCBnew netlist export → PCBnew import
- Correct workflow: schematic → eeschema exports netlist → PCBnew imports (NOT hand-written .net)

## kipy IPC Rotation Direction
- KiCad rotation convention: **-90° = 90° CCW on screen**, +90° = 90° CW on screen
- `Angle.from_degrees(-90)` → visually CCW by 90°
- `Angle.from_degrees(+90)` → visually CW by 90°
- Example: terminal block at rot=+90° (CW) → pad2 (local +x) ends up ABOVE pad1 (lower y on screen)

## Python Plugins / wxPython
- KiCad Python plugins need wxPython in venv
- Install: `pip install wxPython` (compiles from source, ~58MB tarball)
- **Open schematic: `eeschema path/to/file.kicad_sch`** — invoke eeschema DIRECTLY
- NEVER use `kicad.sh` or `kicad` binary — `kicad` is just a launcher wrapper, use the specific tool
- fontconfig error fix: remove leading whitespace before `<?xml` in `~/.config/fontconfig/fonts.conf`
