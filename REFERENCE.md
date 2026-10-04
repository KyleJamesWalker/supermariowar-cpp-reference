# C++ reference for supermariowar-rust

This is upstream [mmatyas/supermariowar](https://github.com/mmatyas/supermariowar) with test hooks added, so the [Rust port](https://github.com/KyleJamesWalker/supermariowar-rust) can be compared against it frame by frame.

## Branches

- `master`: upstream, unmodified (remote `upstream`).
- `harness-latest`: `harness` merged with upstream `5693918f`, the commit the Rust port's `upstream-sync` branch matches, plus the stored-settings fix to the level editor dump.
- `harness-rle`: `harness-latest` plus the `SMW_NO_RLE` build option, which `build-reference.sh` turns on.
- `harness`: upstream `a7f7e25`, the commit the Rust port was first translated from, plus two commits:
  - the game replay and state-dump harness (`tools/cpp-harness.patch` in the Rust repo);
  - the editor harness (`tools/editor-harness.patch`).

Every hook is a no-op unless its `SMW_*` environment variable is set, so a harness build plays like upstream. The Rust repo's `REPLAY.md` and `EDITOR_REPLAY.md` are the spec.

## Build

```sh
./build-reference.sh   # NO_NETWORK, SMW_NO_RLE Release build with -ffp-contract=off, which the parity comparison needs
```

The Rust repo's `tools/run_ref.sh` runs this binary through `SMW_BIN` (default `~/work/smw-ref/build/smw`).

`-ffp-contract=off` stops clang fusing `a*b+c` into FMA instructions, which Rust never does.

`-DSMW_NO_RLE=ON` skips `SDL_SetSurfaceRLE` on sprites, fonts and skins (option default `OFF`, as upstream). The build script turns it on, and the Rust repo's goldens come from that build: on sdl2-compat, RLE surfaces are re-encoded on every blit, and the map foreground shows its magenta colour key.
