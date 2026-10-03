/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef MTP_H
#define MTP_H

#include "rtkit.h"
#include "types.h"

struct mtp_handoff {
    u64 pt_phys;
    size_t pt_size;
    struct rtkit_buffers bfrs;
};

int mtp_init(void);
const struct mtp_handoff *mtp_get_handoff(void);

#endif
