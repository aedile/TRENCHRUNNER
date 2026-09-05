/*
 * e6809.h - MC6809 CPU emulator interface, from vecx (https://github.com/jhawthorn/vecx).
 * Copyright (C) Valavan Manohararajah and the vecx contributors. GPL-3.0; see
 * LICENSES/GPL-3.0.txt. Modified for TRENCHRUNNER on 4 September 2026 to add
 * e6809_get_pc() and e6809_run().
 */
#ifndef __E6809_H
#define __E6809_H

/* user defined read and write functions */

extern unsigned char (*e6809_read8) (unsigned address);
extern void (*e6809_write8) (unsigned address, unsigned char data);

void e6809_reset (void);
unsigned e6809_sstep (unsigned irq_i, unsigned irq_f);
unsigned e6809_get_pc (void);

#endif