#pragma once

// Correctly rounded binary32 functions from CORE-MATH (MIT, see the .c files), so every platform and the
// Rust port compute the same game state.
#ifdef __cplusplus
extern "C" {
#endif
float cr_sinf(float x);
float cr_cosf(float x);
float cr_atan2f(float y, float x);
#ifdef __cplusplus
}
#endif
