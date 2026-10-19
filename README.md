# Gapper — C rewrite

This is a native C/SDL2 rewrite of the supplied 1986 DOS game, `gapper.exe`.
It keeps the original game's CGA-style presentation and core rules while
removing the dependency on DOS and the compiled BASIC runtime.

## Build and run

```sh
make
./gapper
```

The DOS original used CPU-speed-dependent busy loops, so it had no single fixed
real-time speed across an IBM PC, 286, 386, or emulator. The default targets the
faster school-PC feel; use `./gapper --speed 0.75` or `./gapper --speed 1.25` to
adjust the whole simulation if desired.

SDL2 development files are required (`libsdl2-dev` on Debian/Ubuntu,
`sdl2` on Homebrew). Run `make test` for the built-in logic checks.

Controls:

- Arrow keys or WASD: move / queue the next turn
- Space or Enter: leave a temporary gap behind Gapper (costs 5 points)
- Escape: return to the title screen
- `P`, `Q`, `N`, `I`: title-screen choices

The game reads and writes the original `GAPPER.HS` CSV-like high-score format.
Run it from another directory or copy that file first if the supplied historical
score file should remain untouched.

See [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) for the binary analysis and
the boundary between recovered behavior and implementation choices.
