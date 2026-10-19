# Reverse-engineering notes

## Supplied artifacts

- `gapper.exe`: SHA-256
  `70c9729a7fd5e2cd5c0e8d8e887a2c5f3129943a3da30babf1500c7044046558`
- `GAPPER.HS`: SHA-256
  `6e0eb6792f169116d7cbc8afdc56d2342d51947bcd7f7ec2eb3b5f7d460d6505`

`gapper.exe` is a 40,369-byte 16-bit MZ executable. It is an MS-DOS program,
not a Windows PE executable. Runtime error strings, far-call patterns, graphics
entry points, and the embedded `Licensed Material - Program Property of IBM`
text identify an IBM/Microsoft BASIC compiler runtime. The executable contains
roughly 6 KiB of game-specific 8086 code plus a much larger linked runtime.

## Recovered behavior

Static disassembly and execution under DOSBox-X established the following:

- The title screen has Play, Quit, New Player, and Instructions choices.
- The persistent file is `GAPPER.HS`. It contains ten records in the form
  `"ABC",2595,3`, followed by an optional CP/M EOF byte (`0x1a`).
- A player starts with two extra lives. Another life is awarded at each 5,000
  point boundary.
- Gapper travels only on grid edges and paints cyan edges magenta.
- A cell scores 50 points when all four of its edges have been painted.
- Completing the currently lit point box awards 100 points instead of the
  ordinary 50.
- Space/Return creates one temporary impassable gap behind Gapper and costs 5
  points. A second gap cannot be made while the first is active.
- Gapper and the Seeker both respect a live gap.
- The Seeker chooses a direct route at intersections; its delay shrinks as the
  level rises.
- Level dimensions and masks are fixed: levels 1–3 are full 3x3, 4x4, and 5x5
  grids; level 4 is a full 6x7 grid; levels 5 and above use an 8x7 T-shaped
  mask with four full rows above three centered four-cell rows. The original
  stores edge/cell state in flattened arrays with a stride of 21.
- Gapper starts at the upper left and the Seeker at the last lower-right vertex
  in the active mask.
- Embedded DATA blocks contain PUT/GET sprite words and the title artwork.

The original instruction text was also recovered directly from the executable
and is represented in the rewrite without requiring the binary at runtime.

## Rewrite decisions

This is a source-level behavioral port, not an instruction-for-instruction
translation of the BASIC runtime. SDL2 replaces BIOS/CGA graphics, keyboard,
timing, and speaker calls. A small built-in bitmap font replaces ROM text.

The original Seeker uses local coordinate/distance tests rather than a maze
search. The rewrite now does the same: it chooses the dominant coordinate
toward Gapper at each intersection. A live gap therefore makes it wait instead
of finding an artificial route around the gap.
The rewrite uses those recovered fixed board masks and their original cell
spacing. Random selection remains only for the illuminated point box. The
original advances actors in pixel increments inside CPU-speed-dependent busy
loops, so there is no machine-independent canonical real-time speed. The port
uses equivalent pixel-based movement with frame-rate-independent timing and an
optional `--speed` multiplier.
