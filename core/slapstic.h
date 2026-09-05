/*
 * slapstic.h - Atari slapstic 137412-101 bank-switch chip, as used by The Empire Strikes Back.
 *
 * The chip watches accesses inside its 8 KB window (0x8000-0x9FFF here) and switches banks
 * through a small state machine keyed on magic addresses. This follows the model in MAME
 * before its 2022 rewrite (src/mame/machine/slapstic.cpp, BSD-3-Clause, copyright Aaron Giles
 * and the MAME team), which is the one that ran this game: for chip 101 the "alternate"
 * sequence is 0x1DFF (the postbyte fetch of the LDA ,X at 9DFE), then 0x1B5C-0x1B5F (the
 * LDA's data read; the low two bits are the bank), then any 0x0080/0x0090/0x00A0/0x00B0
 * access commits it. The chip only sees window accesses, so feed it nothing else.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { SLAP_DISABLED = 0, SLAP_ENABLED, SLAP_ALT2, SLAP_ALT3, SLAP_BIT1, SLAP_BIT2, SLAP_BIT3 };

extern uint8_t slapstic_state;         /* SLAP_* */
extern uint8_t slapstic_current_bank;  /* 0..3 */

/* Reset to the power-on bank (3), disabled. */
void slapstic_reset(void);

/* One access inside the window: offset = address & 0x1FFF. Call AFTER the data has been read
 * (the byte comes from the bank in force before the access). Returns the current bank. */
int slapstic_tweak(uint16_t offset);

#ifdef __cplusplus
}
#endif
