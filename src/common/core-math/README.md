`src/binary32/sin/sinf.c`, `cos/cosf.c` and `atan2/atan2f.c` from
[CORE-MATH](https://core-math.gitlabpages.inria.fr/) commit `284b3b0e198042c38f5c30316f696786b10816b0`,
MIT-licensed (notice in each file). Built with `-ffp-contract=off` like the rest of the reference.

One local change: `sinf.c` and `cosf.c` use their own `roundeven_finite` on `_WIN32`, because MinGW's libm has
no `roundeven` for `__builtin_roundeven` to call.
