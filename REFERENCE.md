# C++ reference for supermariowar-rust

This is upstream [mmatyas/supermariowar](https://github.com/mmatyas/supermariowar) with test hooks added, so the [Rust port](https://github.com/KyleJamesWalker/supermariowar-rust) can be compared against it frame by frame.

## Branches

- `master`: upstream, unmodified (remote `upstream`).
- `harness`: upstream `a7f7e25`, the commit the Rust port was translated from, plus two commits:
  - the game replay and state-dump harness (`tools/cpp-harness.patch` in the Rust repo);
  - the editor harness (`tools/editor-harness.patch`).

Every hook is a no-op unless its `SMW_*` environment variable is set, so a harness build plays like upstream. The Rust repo's `REPLAY.md` and `EDITOR_REPLAY.md` are the spec.

## Build

```sh
./build-reference.sh   # NO_NETWORK Release build with -ffp-contract=off, which the parity comparison needs
```

The Rust repo's `tools/run_ref.sh` runs this binary through `SMW_BIN` (default `~/work/smw-ref/build/smw`).

`-ffp-contract=off` stops clang fusing `a*b+c` into FMA instructions, which Rust never does. `-DDISABLE_DEFAULT_CFLAGS=ON` works around flags in `cmake/PlatformArm` that clang on arm64 rejects.
