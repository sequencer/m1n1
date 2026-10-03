/* SPDX-License-Identifier: MIT */

#ifndef RVBAR_H
#define RVBAR_H

/*
 * RVBAR mailbox for the OS. iBoot locks every core's RVBAR to m1n1's
 * _vectors_start, so a core that the OS powers on (secondary start, CPU
 * hotplug) or that comes out of S2R starts in m1n1's reset vector. XNU's
 * reset vector does the same dispatch through ResetHandlerData; per
 * ml_arm_sleep, on current SoCs the reconfig engine restores the SoC and
 * releases the boot CPU at the reset vector without going through iBoot.
 *
 * The mailbox lives in the first page of .init, together with the vectors and
 * the dispatcher, and the OS keeps that page reserved. The OS fills one slot
 * per core: {MPIDR | RVBAR_SLOT_VALID, entry}. A core whose MPIDR has a slot
 * with a non-zero entry jumps there at EL2 with the MMU off; any other reset
 * takes m1n1's normal path. iBoot reloads the image on a cold boot, which
 * clears the mailbox.
 */
#define RVBAR_MAILBOX_MAGIC 0x4b414c3252564241 /* "ABVR2LAK" */
#define RVBAR_MAILBOX_SLOTS 24
#ifdef __ASSEMBLER__
#define RVBAR_SLOT_VALID (1 << 63)
#else
#define RVBAR_SLOT_VALID (1UL << 63)
#endif

#define RVBAR_MAILBOX_OFF_MAGIC 0
#define RVBAR_MAILBOX_OFF_SEEN  8 /* last reset: MPIDR, bit 62 entered, bit 61 jumped */
#define RVBAR_MAILBOX_OFF_SLOTS 16

#ifndef __ASSEMBLER__

#include "types.h"

struct rvbar_slot {
    u64 mpidr;
    u64 entry;
};

struct rvbar_mailbox {
    u64 magic;
    u64 reserved;
    struct rvbar_slot slots[RVBAR_MAILBOX_SLOTS];
};

extern struct rvbar_mailbox rvbar_mailbox;

#endif

#endif
