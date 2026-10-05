# C++ reference for supermariowar-rust

This is upstream [mmatyas/supermariowar](https://github.com/mmatyas/supermariowar) with test hooks added, so the [Rust port](https://github.com/KyleJamesWalker/supermariowar-rust) can be compared against it frame by frame.

## Branches

- `harness-latest` (default): upstream `5693918f` plus the game and editor harness and the `SMW_NO_RLE` option. Changes to the harness land here through PRs, and the Rust port's goldens and `tools/*-harness.patch` (`git diff master harness-latest`) come from it.
- `master`: upstream, unmodified (remote `upstream`). Fixes meant for upstream branch from here, so their PRs carry no harness code.
- Tag `port-base-a7f7e25`: upstream `a7f7e25`, the commit the Rust port was first translated from, plus the two original harness commits.

To sync a newer upstream, the Rust repo's `tools/upstream_sync.sh` merges it into a `harness-<sha>` branch from `harness-latest`; that lands back here as a PR once the port matches.

Every hook is a no-op unless its `SMW_*` environment variable is set, so a harness build plays like upstream. The exception is trigonometry: the game's `sinf`, `cosf` and `atan2f` come from [CORE-MATH](https://core-math.gitlabpages.inria.fr/) (`src/common/core-math/`, correctly rounded, MIT), which the Rust port also uses, so every platform computes the same game state. The platform libms (macOS, glibc, Emscripten's musl) round differently in the last bit. The Rust repo's `REPLAY.md` and `EDITOR_REPLAY.md` are the spec.

## Build

```sh
./build-reference.sh   # NO_NETWORK, SMW_NO_RLE Release build with -ffp-contract=off, which the parity comparison needs
```

The Rust repo's `tools/run_ref.sh` runs `build/smw` from this checkout (`SMW_REF_DIR`, default `~/work/supermariowar-cpp-reference`), or the binary `SMW_BIN` names.

`-ffp-contract=off` stops clang fusing `a*b+c` into FMA instructions, which Rust never does.

`-DSMW_NO_RLE=ON` skips `SDL_SetSurfaceRLE` on sprites, fonts and skins (option default `OFF`, as upstream). The build script turns it on, and the Rust repo's goldens come from that build: on sdl2-compat, RLE surfaces are re-encoded on every blit, and the map foreground shows its magenta colour key.
