#pragma once
#include <stdint.h>
#include "starwars.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_init(void);              /* buttons, battery enable, IMU */
void input_update(sw_input_t *in);  /* call once per loop; fills the game's input struct */

#ifdef __cplusplus
}
#endif
