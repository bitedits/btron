/*
 * btron/smp.h — B-System SMP / multi-CPU public interface
 *
 * Cleanroom implementation. No TianoCore EDK II dependency.
 * Consulted: Haiku src/system/boot/platform/efi/arch/x86/arch_smp.cpp
 *            NetBSD sys/arch/amd64/include/apic.h
 *            Intel 64 and IA-32 Architectures SDM Vol.3A §10.6
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _BTRON_SMP_H_
#define _BTRON_SMP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTRON_SMP_MAX_CPUS        64
#define BTRON_LAPIC_DEFAULT_BASE  UINT64_C(0xFEE00000)

/* LAPIC register offsets */
#define LAPIC_ID_REG          0x0020
#define LAPIC_VER_REG         0x0030
#define LAPIC_TPR             0x0080
#define LAPIC_EOI             0x00B0
#define LAPIC_LDR             0x00D0
#define LAPIC_SVR             0x00F0
#define LAPIC_ICR_LO          0x0300
#define LAPIC_ICR_HI          0x0310
#define LAPIC_TIMER           0x0320
#define LAPIC_ERROR_STATUS    0x0280
#define LAPIC_INIT_COUNT      0x0380
#define LAPIC_CURR_COUNT      0x0390
#define LAPIC_DIV_CFG         0x03E0

#define LAPIC_DM_FIXED        (0x0 << 8)
#define LAPIC_DM_INIT         (0x5 << 8)
#define LAPIC_DM_STARTUP      (0x6 << 8)

#define LAPIC_LEVEL_ASSERT    (1 << 14)
#define LAPIC_LEVEL_DEASSERT  (0 << 14)
#define LAPIC_TRIGGER_LEVEL   (1 << 15)
#define LAPIC_TRIGGER_EDGE    (0 << 15)
#define LAPIC_PENDING         (1 << 12)
#define LAPIC_SVR_ENABLE      (1 << 8)

#define MADT_TYPE_LOCAL_APIC          0
#define MADT_TYPE_IO_APIC             1
#define MADT_TYPE_INT_OVERRIDE        2
#define MADT_TYPE_LOCAL_APIC_NMI      4
#define MADT_TYPE_LOCAL_X2APIC        9

#define MADT_LAPIC_ENABLED            (1 << 0)
#define MADT_LAPIC_ONLINE_CAPABLE     (1 << 1)

#define BTRON_SMP_TRAMPOLINE_PHYS     0x9000U
#define BTRON_SMP_AP_STACK_SIZE       (16 * 1024)

typedef struct btron_cpu_entry {
    uint8_t  apic_id;
    uint8_t  apic_version;
    uint8_t  online;
    uint8_t  is_bsp;
    void    *stack_top;
} btron_cpu_entry_t;

extern volatile uint32_t  g_cpu_ready[BTRON_SMP_MAX_CPUS];
extern btron_cpu_entry_t  g_cpu_topology[BTRON_SMP_MAX_CPUS];
extern volatile uint32_t  g_num_cpus;
extern volatile uint32_t  g_cpus_online;

int  btron_smp_parse_madt(const void *rsdp_ptr);
int  btron_smp_prepare_aps(void);
int  btron_smp_boot_aps(void);
void btron_smp_ap_entry(uint32_t cpu_idx);
uint32_t btron_cpu_id(void);
void btron_smp_spin_us(uint32_t us);
void btron_scheduler_tick(void);
void btron_scheduler_dispatch(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SMP_H_ */
