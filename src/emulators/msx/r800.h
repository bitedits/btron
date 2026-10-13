/*
 * r800.h — ASCII R800 CPU emulation for MSX turbo R.
 * Hardware emulation source for B-MSX.
 */

#ifndef _BTRON_EMULATOR_MSX_R800_H_
#define _BTRON_EMULATOR_MSX_R800_H_

#include "z80.h"

/* R800 shares the exact register set with Z80 */
typedef z80 r800_t;

void r800_init(r800_t *r);
void r800_step(r800_t *r);

#endif /* _BTRON_EMULATOR_MSX_R800_H_ */
