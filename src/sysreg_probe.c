/* SPDX-License-Identifier: MIT */

/*
 * T8132: find which of the CPU settings SPTM makes before XNU runs lets EL2
 * write s3_5_c15_c6_2 (XNU arm64_prepare_for_sleep(1), K:fffffe000bc3164c),
 * which UNDEFs under a raw m1n1 boot. Runs on the boot CPU with the MMU off,
 * right before the next stage, when the payload carries "sysregprobe=1".
 * Each step makes one of SPTM's cold-boot writes (sptm.t8132 S:af0cc..,
 * artifacts/driver-debug/s2r-27/agent-sptm-el2-setup.md) and then writes
 * c6_2 back with its own value; faulting accesses are skipped by the
 * exception guard and reported.
 */

#include "cpu_regs.h"
#include "exception.h"
#include "sysreg_probe.h"
#include "utils.h"

bool sysreg_probe_enabled;

#define C6_2 sys_reg(3, 5, 15, 6, 2)

#define GUARDED(stmt)                                                                              \
    ({                                                                                             \
        int _c = exc_count;                                                                        \
        exc_guard = GUARD_SKIP | GUARD_SILENT;                                                     \
        stmt;                                                                                      \
        sysop("isb");                                                                              \
        exc_guard = GUARD_OFF;                                                                     \
        exc_count == _c;                                                                           \
    })

static u64 saved[8];

static void probe_c6_2(const char *after)
{
    u64 v = 0;
    bool rd = GUARDED(v = mrs(C6_2));
    bool wr = rd && GUARDED(msr(C6_2, v));

    printf("sysreg-probe: after %s: c6_2 read %s (0x%lx), write %s\n", after, rd ? "ok" : "FAULT", v,
           wr ? "OK" : "fault");
}

#define STEP(i, name, reg, expr)                                                                   \
    do {                                                                                           \
        u64 _old = 0, _new = 0;                                                                    \
        bool _rd = GUARDED(_old = mrs(reg));                                                       \
        saved[i] = _old;                                                                           \
        u64 _val = (expr);                                                                         \
        bool _wr = GUARDED(msr(reg, _val));                                                        \
        GUARDED(_new = mrs(reg));                                                                  \
        printf("sysreg-probe: %s: read %s 0x%lx, write 0x%lx %s, now 0x%lx\n", name,              \
               _rd ? "ok" : "FAULT", _old, _val, _wr ? "ok" : "FAULT", _new);                      \
        probe_c6_2(name);                                                                          \
    } while (0)

void sysreg_probe_run(void)
{
    printf("sysreg-probe: CurrentEL %ld, HCR_EL2 0x%lx\n", mrs(CurrentEL) >> 2, mrs(HCR_EL2));
    probe_c6_2("start");

    /* SPTM cold boot, boot CPU, in order (all before GXF is enabled) */
    STEP(0, "s3_1_c15_c9_4 = 0", sys_reg(3, 1, 15, 9, 4), 0);
    STEP(1, "SPRR_CONFIG_EL1 = 1", SYS_IMP_APL_SPRR_CONFIG_EL1, 1);
    STEP(2, "SPRR_PERM_EL1 = 0x2020a52a302abaf5", SYS_IMP_APL_SPRR_PERM_EL1, 0x2020a52a302abaf5);
    STEP(3, "APCTL_EL1 |= 0x11 &= ~0x6", SYS_IMP_APL_APCTL_EL1, (_old | 0x11) & ~0x6UL);
    STEP(4, "s3_4_c15_c15_4 = 2", sys_reg(3, 4, 15, 15, 4), 2);
    STEP(5, "s3_4_c15_c12_0 |= 0x18", sys_reg(3, 4, 15, 12, 0), _old | 0x18);
    STEP(6, "s3_5_c15_c1_3 = 0", sys_reg(3, 5, 15, 1, 3), 0);
    STEP(7, "GXF_CONFIG_EL1 = 1", SYS_IMP_APL_GXF_CONFIG_EL1, 1);

    /* Back to what iBoot handed over, in reverse, so the next stage sees no change. */
    GUARDED(msr(SYS_IMP_APL_GXF_CONFIG_EL1, saved[7]));
    GUARDED(msr(sys_reg(3, 5, 15, 1, 3), saved[6]));
    GUARDED(msr(sys_reg(3, 4, 15, 12, 0), saved[5]));
    GUARDED(msr(sys_reg(3, 4, 15, 15, 4), saved[4]));
    GUARDED(msr(SYS_IMP_APL_APCTL_EL1, saved[3]));
    GUARDED(msr(SYS_IMP_APL_SPRR_PERM_EL1, saved[2]));
    GUARDED(msr(SYS_IMP_APL_SPRR_CONFIG_EL1, saved[1]));
    GUARDED(msr(sys_reg(3, 1, 15, 9, 4), saved[0]));
    probe_c6_2("restore");
    printf("sysreg-probe: done\n");
}
