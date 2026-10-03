/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * T8132 MTP is started once, here, and handed to the next stages running.
 *
 * iBoot loads the J713 MTP firmware into SRAM once, and the firmware cannot be
 * initialized a second time: when its OSLog endpoint is started again after a
 * full start, it re-walks the OSLog source list from the context at 0x10ca0a0,
 * whose first node (0x106e750) has meanwhile had its link at +0x28 reused for a
 * function pointer table, and faults in strlen (0x10381cc). So the stage that
 * starts MTP has to be the only one; U-Boot and Linux attach to this session.
 *
 * The RTKit buffers and the dart-mtp page tables live in memory taken from the
 * top of RAM, which the FDT keeps reserved; the buffers keep their IOVAs in
 * Linux through memory-region / iommu-addresses, and dart-mtp is locked so that
 * nothing in between resets the stream.
 */

#include "mtp.h"
#include "adt.h"
#include "asc.h"
#include "dapf.h"
#include "dart.h"
#include "iova.h"
#include "pmgr.h"
#include "rtkit.h"
#include "soc.h"
#include "utils.h"

#define MTP_ASC  "/arm-io/mtp"
#define MTP_DART "/arm-io/dart-mtp"
/* ADT dart-mtp/mapper-mtp reg */
#define MTP_SID 0

/* Page tables (L1 + L2) and the RTKit buffers of the session. */
#define MTP_PT_SIZE  (2 * SZ_16K)
#define MTP_BFR_SIZE (16 * SZ_16K)

/* Startup is over once the IOP has been quiet this long. */
#define MTP_SETTLE_US (200 * 1000)
#define MTP_START_US  (2 * 1000 * 1000)

static bool mtp_started;
static struct mtp_handoff mtp_state;

int mtp_init(void)
{
    if (mtp_started || chip_id != T8132 || adt_path_offset(adt, MTP_ASC) < 0)
        return 0;

    /*
     * MTP reaches its memory only through the dart-mtp DAPF, and on T8132
     * programming the filters needs the ANE domain on.
     */
    pmgr_adt_power_enable("/arm-io/ane");
    if (dapf_init(MTP_DART, 1) < 0)
        return -1;

    dart_dev_t *dart = dart_init_adt(MTP_DART, 0, MTP_SID, false);
    if (!dart)
        return -1;

    u64 pt = top_of_memory_alloc(MTP_PT_SIZE);
    if (dart_use_pt_pool(dart, pt, MTP_PT_SIZE) < 0)
        return -1;

    /* The IOVA allocator wants a 32 MiB aligned base inside the dart-mtp window. */
    u64 iova_base = ALIGN_UP(dart_vm_base(dart), SZ_32M);
    iova_domain_t *iovad = iovad_init(iova_base, iova_base + MTP_BFR_SIZE);
    if (!iovad)
        return -1;

    asc_dev_t *asc = asc_init(MTP_ASC);
    if (!asc)
        return -1;

    rtkit_dev_t *rtk = rtkit_init("mtp", asc, dart, iovad, NULL, false);
    if (!rtk)
        return -1;

    u64 bfr = top_of_memory_alloc(MTP_BFR_SIZE);
    rtkit_use_buffer_pool(rtk, bfr, MTP_BFR_SIZE);

    if (!rtkit_boot(rtk)) {
        printf("mtp: RTKit boot failed\n");
        return -1;
    }

    /* Answer the startup traffic (ioreport, OSLog, syslog) until it stops. */
    u64 deadline = timeout_calculate(MTP_START_US);
    u64 quiet = timeout_calculate(MTP_SETTLE_US);
    while (!timeout_expired(quiet) && !timeout_expired(deadline)) {
        struct rtkit_message msg;

        if (!rtkit_can_recv(rtk))
            continue;
        quiet = timeout_calculate(MTP_SETTLE_US);

        int ret = rtkit_recv(rtk, &msg);
        if (ret < 0) {
            printf("mtp: RTKit failed during startup\n");
            return -1;
        }
        if (ret == 1)
            printf("mtp: message to endpoint 0x%02x during startup: 0x%lx\n", msg.ep, msg.msg);
    }

    dart_lock_adt(MTP_DART, 0);

    mtp_state.pt_phys = pt;
    mtp_state.pt_size = MTP_PT_SIZE;
    rtkit_get_buffers(rtk, &mtp_state.bfrs);
    mtp_started = true;

    printf("mtp: running, handed to the next stage\n");
    return 0;
}

const struct mtp_handoff *mtp_get_handoff(void)
{
    return mtp_started ? &mtp_state : NULL;
}
