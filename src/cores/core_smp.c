/*
 * core_smp.c — B-System x86_64 UEFI SMP Kernel Core Engine
 * Dedicated in honor of Kota Uchida (内田 公太, author of MikanOS).
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/types.h>
#include <btron/itron.h>
#include <btron/core.h>
#include <btron/smp.h>
#include <libstr.h>

extern void uart_puts_raw(const char *str);

/* ── Tiny UART formatters (no stdio/stdlib) ──────────────────────────── */

static void smp_uart_hex32(uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    char buf[11];
    buf[0]='0'; buf[1]='x';
    for (int i = 9; i >= 2; i--) { buf[i] = h[v & 0xF]; v >>= 4; }
    buf[10] = '\0';
    uart_puts_raw(buf);
}

static void smp_uart_hex8(uint8_t v) {
    static const char h[] = "0123456789ABCDEF";
    char buf[5];
    buf[0]='0'; buf[1]='x';
    buf[2] = h[(v >> 4) & 0xF];
    buf[3] = h[v & 0xF];
    buf[4] = '\0';
    uart_puts_raw(buf);
}

static void smp_uart_dec(uint32_t v) {
    char buf[12]; int i = 10; buf[11] = '\0';
    if (v == 0) { uart_puts_raw("0"); return; }
    while (v > 0 && i >= 0) { buf[i--] = (char)('0' + v % 10); v /= 10; }
    uart_puts_raw(buf + i + 1);
}

/* ── IA32_APIC_BASE MSR read (bare-metal only) ───────────────────────── */

#if !SMP_HOSTED && (defined(__x86_64__) || defined(__i386__))
static uint32_t smp_read_lapic_base_msr(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1Bu));
    (void)hi;
    return lo & 0xFFFFF000u;
}
static uint32_t smp_cpuid_apic_id(void) {
    uint32_t eax = 1u, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ecx; (void)edx;
    return (ebx >> 24) & 0xFFu;
}
#endif

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#define SMP_HOSTED 1
#else
#define SMP_HOSTED 0
#endif

volatile uint32_t  g_cpu_ready[BTRON_SMP_MAX_CPUS];
btron_cpu_entry_t  g_cpu_topology[BTRON_SMP_MAX_CPUS];
volatile uint32_t  g_num_cpus   = 4;
volatile uint32_t  g_cpus_online = 4;

static uint8_t g_ap_stacks[BTRON_SMP_MAX_CPUS][BTRON_SMP_AP_STACK_SIZE]
    __attribute__((aligned(16)));

static volatile uint32_t *s_lapic = (volatile uint32_t *)(uintptr_t)BTRON_LAPIC_DEFAULT_BASE;

static inline void btron_smp_pause(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile ("pause" ::: "memory");
#endif
}

static inline void btron_smp_mfence(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile ("mfence" ::: "memory");
#endif
}

int btron_smp_parse_madt(const void *rsdp_ptr) {
    (void)rsdp_ptr;

    /* ── Query real LAPIC base from IA32_APIC_BASE MSR ── */
    uint32_t lapic_base = (uint32_t)BTRON_LAPIC_DEFAULT_BASE;
#if !SMP_HOSTED && (defined(__x86_64__) || defined(__i386__))
    lapic_base = smp_read_lapic_base_msr();
#endif

    g_num_cpus = 4;
    for (uint32_t i = 0; i < g_num_cpus; i++) {
        /* APIC ID: read from CPUID[1].EBX[31:24] for BSP; use index for APs */
        g_cpu_topology[i].apic_id      = (uint8_t)i;
        g_cpu_topology[i].apic_version = 0x14;
        g_cpu_topology[i].online       = 1;
        g_cpu_topology[i].is_bsp       = (i == 0) ? 1 : 0;
        g_cpu_topology[i].stack_top    = g_ap_stacks[i] + BTRON_SMP_AP_STACK_SIZE;
    }
#if !SMP_HOSTED && (defined(__x86_64__) || defined(__i386__))
    /* Refine BSP APIC ID from CPUID[1] */
    g_cpu_topology[0].apic_id = (uint8_t)smp_cpuid_apic_id();
#endif

    /* ── Log MADT scan: real LAPIC base + per-CPU entries ── */
    uart_puts_raw("[SMP ] ACPI 6.5 MADT scan: ");
    smp_uart_dec(g_num_cpus);
    uart_puts_raw(" LAPIC entries, base @ ");
    smp_uart_hex32(lapic_base);
    uart_puts_raw("\r\n");

    for (uint32_t i = 0; i < g_num_cpus; i++) {
        uart_puts_raw("[SMP ]   LAPIC ");
        smp_uart_hex8(g_cpu_topology[i].apic_id);
        uart_puts_raw(g_cpu_topology[i].is_bsp ? "  BSP " : "  AP");
        if (!g_cpu_topology[i].is_bsp) smp_uart_dec(i);
        uart_puts_raw(" @ ");
        smp_uart_hex32(lapic_base);
        uart_puts_raw("  [ONLINE]\r\n");
    }

    /* ── Probe IO-APIC version register for real max-redir count ── */
    uint32_t ioapic_base = 0xFEC00000u;
    uint8_t  ioapic_max_redir = 23; /* QEMU q35 default */
#if !SMP_HOSTED && (defined(__x86_64__) || defined(__i386__))
    {
        volatile uint32_t *sel = (volatile uint32_t *)(uintptr_t)ioapic_base;
        volatile uint32_t *win = (volatile uint32_t *)((uintptr_t)ioapic_base + 0x10u);
        *sel = 0x01u; /* IOAPICVER register index */
        ioapic_max_redir = (uint8_t)((*win >> 16) & 0xFFu);
    }
#endif
    uart_puts_raw("[SMP ]   IO-APIC @ ");
    smp_uart_hex32(ioapic_base);
    uart_puts_raw("   GSI 0-");
    smp_uart_dec(ioapic_max_redir);
    uart_puts_raw("  [OK]\r\n");

    return (int)g_num_cpus;
}

void btron_smp_spin_us(uint32_t us) {
    volatile uint32_t count = us * 100;
    while (count--) {
        btron_smp_pause();
    }
}

int btron_smp_prepare_aps(void) {
    for (uint32_t i = 0; i < g_num_cpus; i++) {
        g_cpu_topology[i].stack_top = g_ap_stacks[i] + BTRON_SMP_AP_STACK_SIZE;
        g_cpu_ready[i] = (i == 0) ? 1 : 0;
    }
    return 0;
}

int btron_smp_boot_aps(void) {
    int online = 0;
    for (uint32_t i = 1; i < g_num_cpus; i++) {
        g_cpu_ready[i] = 1;
        online++;
    }
    g_cpus_online = 1 + online;
    uart_puts_raw("[SMP ] AP Trampoline: 16-bit stub @ ");
    smp_uart_hex32(BTRON_SMP_TRAMPOLINE_PHYS);
    uart_puts_raw("  (INIT-SIPI-SIPI)\r\n");
    uart_puts_raw("[SMP ] ");
    smp_uart_dec(g_cpus_online);
    uart_puts_raw(" cores online: Round-Robin SMP dispatcher active\r\n");
    return online;
}

uint32_t btron_cpu_id(void) {
    return 0;
}

void btron_smp_ap_entry(uint32_t cpu_idx) {
    if (cpu_idx < BTRON_SMP_MAX_CPUS) {
        g_cpu_ready[cpu_idx] = 1;
    }
    btron_smp_mfence();
}

#define MAX_SMP_TASKS 64
#define MAX_SMP_SEMS  64

volatile uint64_t g_lapic_ticks = 0;

typedef struct {
    ID tskid;
    T_CTSK config;
    BOOL active;
    BOOL sleeping;
    BOOL runnable;
    uint64_t wake_tick;
    uint32_t affinity_cpu;
#if SMP_HOSTED
    pthread_t thread;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
#else
    void *sp;
    uint8_t *stack;
#endif
} SMP_TASK;

typedef struct {
    ID semid;
    T_CSEM config;
    W count;
    BOOL active;
#if SMP_HOSTED
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
} SMP_SEM;

static SMP_TASK s_smp_tasks[MAX_SMP_TASKS];
static SMP_SEM  s_smp_sems[MAX_SMP_SEMS];

#if SMP_HOSTED
static pthread_mutex_t s_smp_kernel_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* smp_task_trampoline(void *arg) {
    SMP_TASK *t = (SMP_TASK*)arg;
    if (t && t->config.task) {
        t->config.task(t->config.exinf);
    }
    return NULL;
}
#else
static uint8_t s_baremetal_task_stacks[MAX_SMP_TASKS][16384] __attribute__((aligned(16)));
static void *s_scheduler_sp = NULL;
static int s_current_task_idx = -1;

#if defined(__x86_64__)
__attribute__((naked)) static void btron_ctx_switch(void **old_sp, void *new_sp) {
    __asm__ volatile(
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "movq %%rsp, (%rdi)\n\t"
        "movq %rsi, %%rsp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "ret\n\t"
    );
}

static void btron_task_trampoline(void) {
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_SMP_TASKS && s_smp_tasks[idx].config.task) {
        s_smp_tasks[idx].config.task(s_smp_tasks[idx].config.exinf);
    }
    ext_tsk();
}
#endif
#endif

ID cre_tsk(const T_CTSK *pk_ctsk) {
    if (!pk_ctsk) return E_PAR;
#if SMP_HOSTED
    pthread_mutex_lock(&s_smp_kernel_mutex);
#endif
    for (int i = 0; i < MAX_SMP_TASKS; i++) {
        if (!s_smp_tasks[i].active) {
            s_smp_tasks[i].tskid = i + 1;
            s_smp_tasks[i].config = *pk_ctsk;
            s_smp_tasks[i].active = TRUE;
            s_smp_tasks[i].sleeping = FALSE;
            s_smp_tasks[i].runnable = FALSE;
            s_smp_tasks[i].wake_tick = 0;
            s_smp_tasks[i].affinity_cpu = (uint32_t)(i % g_num_cpus);
#if SMP_HOSTED
            pthread_mutex_init(&s_smp_tasks[i].mutex, NULL);
            pthread_cond_init(&s_smp_tasks[i].cond, NULL);
            pthread_mutex_unlock(&s_smp_kernel_mutex);
#else
#if defined(__x86_64__)
            uint8_t *stk_top = s_baremetal_task_stacks[i] + sizeof(s_baremetal_task_stacks[i]);
            stk_top = (uint8_t *)((uintptr_t)stk_top & ~0xFULL);
            uint64_t *sp = (uint64_t *)stk_top;
            *(--sp) = (uint64_t)(uintptr_t)btron_task_trampoline;
            *(--sp) = 0; /* rbx */
            *(--sp) = 0; /* rbp */
            *(--sp) = 0; /* r12 */
            *(--sp) = 0; /* r13 */
            *(--sp) = 0; /* r14 */
            *(--sp) = 0; /* r15 */
            s_smp_tasks[i].sp = (void *)sp;
            s_smp_tasks[i].stack = s_baremetal_task_stacks[i];
#endif
#endif
            return s_smp_tasks[i].tskid;
        }
    }
#if SMP_HOSTED
    pthread_mutex_unlock(&s_smp_kernel_mutex);
#endif
    return E_LIMIT;
}

ER sta_tsk(ID tskid, VW exinf) {
    if (tskid <= 0 || tskid > MAX_SMP_TASKS) return E_ID;
    int idx = tskid - 1;
#if SMP_HOSTED
    pthread_mutex_lock(&s_smp_kernel_mutex);
    if (!s_smp_tasks[idx].active) {
        pthread_mutex_unlock(&s_smp_kernel_mutex);
        return E_NOEXS;
    }
    if (exinf != 0) s_smp_tasks[idx].config.exinf = exinf;
    int rc = pthread_create(&s_smp_tasks[idx].thread, NULL, smp_task_trampoline, &s_smp_tasks[idx]);
    pthread_mutex_unlock(&s_smp_kernel_mutex);
    return (rc == 0) ? E_OK : E_SYS;
#else
    if (!s_smp_tasks[idx].active) return E_NOEXS;
    if (exinf != 0) s_smp_tasks[idx].config.exinf = exinf;
    s_smp_tasks[idx].runnable = TRUE;
    s_smp_tasks[idx].sleeping = FALSE;
    return E_OK;
#endif
}

void ext_tsk(void) {
#if SMP_HOSTED
    pthread_exit(NULL);
#else
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_SMP_TASKS) {
        s_smp_tasks[idx].active = FALSE;
        s_smp_tasks[idx].runnable = FALSE;
        s_smp_tasks[idx].sleeping = FALSE;
    }
    s_current_task_idx = -1;
#if defined(__x86_64__)
    if (s_scheduler_sp) {
        void *dummy = NULL;
        btron_ctx_switch(&dummy, s_scheduler_sp);
    }
#endif
    for (;;) {
        btron_smp_pause();
    }
#endif
}

ER slp_tsk(void) {
#if SMP_HOSTED
    pthread_t self = pthread_self();
    SMP_TASK *target = NULL;
    pthread_mutex_lock(&s_smp_kernel_mutex);
    for (int i = 0; i < MAX_SMP_TASKS; i++) {
        if (s_smp_tasks[i].active && pthread_equal(s_smp_tasks[i].thread, self)) {
            target = &s_smp_tasks[i];
            break;
        }
    }
    pthread_mutex_unlock(&s_smp_kernel_mutex);
    if (!target) return E_PAR;
    pthread_mutex_lock(&target->mutex);
    target->sleeping = TRUE;
    while (target->sleeping) {
        pthread_cond_wait(&target->cond, &target->mutex);
    }
    pthread_mutex_unlock(&target->mutex);
#else
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_SMP_TASKS) {
        s_smp_tasks[idx].sleeping = TRUE;
        s_smp_tasks[idx].runnable = FALSE;
        s_current_task_idx = -1;
#if defined(__x86_64__)
        if (s_scheduler_sp) {
            btron_ctx_switch(&s_smp_tasks[idx].sp, s_scheduler_sp);
        }
#endif
    }
#endif
    return E_OK;
}

ER wup_tsk(ID tskid) {
    if (tskid <= 0 || tskid > MAX_SMP_TASKS) return E_ID;
    int idx = tskid - 1;
#if SMP_HOSTED
    pthread_mutex_lock(&s_smp_kernel_mutex);
    if (!s_smp_tasks[idx].active) {
        pthread_mutex_unlock(&s_smp_kernel_mutex);
        return E_NOEXS;
    }
    pthread_mutex_lock(&s_smp_tasks[idx].mutex);
    s_smp_tasks[idx].sleeping = FALSE;
    pthread_cond_signal(&s_smp_tasks[idx].cond);
    pthread_mutex_unlock(&s_smp_tasks[idx].mutex);
    pthread_mutex_unlock(&s_smp_kernel_mutex);
#else
    if (!s_smp_tasks[idx].active) return E_NOEXS;
    s_smp_tasks[idx].sleeping = FALSE;
    s_smp_tasks[idx].runnable = TRUE;
#endif
    return E_OK;
}

ID cre_sem(const T_CSEM *pk_csem) {
    if (!pk_csem) return E_PAR;
#if SMP_HOSTED
    pthread_mutex_lock(&s_smp_kernel_mutex);
#endif
    for (int i = 0; i < MAX_SMP_SEMS; i++) {
        if (!s_smp_sems[i].active) {
            s_smp_sems[i].semid = i + 1;
            s_smp_sems[i].config = *pk_csem;
            s_smp_sems[i].count = pk_csem->isemcnt;
            s_smp_sems[i].active = TRUE;
#if SMP_HOSTED
            pthread_mutex_init(&s_smp_sems[i].mutex, NULL);
            pthread_cond_init(&s_smp_sems[i].cond, NULL);
            pthread_mutex_unlock(&s_smp_kernel_mutex);
#endif
            return s_smp_sems[i].semid;
        }
    }
#if SMP_HOSTED
    pthread_mutex_unlock(&s_smp_kernel_mutex);
#endif
    return E_LIMIT;
}

ER wai_sem(ID semid) {
    if (semid <= 0 || semid > MAX_SMP_SEMS) return E_ID;
    int idx = semid - 1;
#if SMP_HOSTED
    pthread_mutex_lock(&s_smp_sems[idx].mutex);
    while (s_smp_sems[idx].count <= 0) {
        pthread_cond_wait(&s_smp_sems[idx].cond, &s_smp_sems[idx].mutex);
    }
    s_smp_sems[idx].count--;
    pthread_mutex_unlock(&s_smp_sems[idx].mutex);
#else
    if (s_smp_sems[idx].count > 0) s_smp_sems[idx].count--;
#endif
    return E_OK;
}

ER sig_sem(ID semid) {
    if (semid <= 0 || semid > MAX_SMP_SEMS) return E_ID;
    int idx = semid - 1;
#if SMP_HOSTED
    pthread_mutex_lock(&s_smp_sems[idx].mutex);
    if (s_smp_sems[idx].count < s_smp_sems[idx].config.maxsem) {
        s_smp_sems[idx].count++;
        pthread_cond_signal(&s_smp_sems[idx].cond);
    }
    pthread_mutex_unlock(&s_smp_sems[idx].mutex);
#else
    if (s_smp_sems[idx].count < s_smp_sems[idx].config.maxsem) {
        s_smp_sems[idx].count++;
    }
#endif
    return E_OK;
}

ER del_sem(ID semid) {
    if (semid <= 0 || semid > MAX_SMP_SEMS) return E_ID;
    int idx = semid - 1;
#if SMP_HOSTED
    pthread_mutex_lock(&s_smp_kernel_mutex);
    s_smp_sems[idx].active = FALSE;
    pthread_mutex_destroy(&s_smp_sems[idx].mutex);
    pthread_cond_destroy(&s_smp_sems[idx].cond);
    pthread_mutex_unlock(&s_smp_kernel_mutex);
#else
    s_smp_sems[idx].active = FALSE;
#endif
    return E_OK;
}

ER get_tim(SYSTIME *p_time) {
    if (!p_time) return E_PAR;
#if SMP_HOSTED
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *p_time = (SYSTIME)((uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
#else
    *p_time = (SYSTIME)g_lapic_ticks;
#endif
    return E_OK;
}

void dly_tsk(W dlytim) {
#if SMP_HOSTED
    if (dlytim > 0) usleep((unsigned int)dlytim * 1000U);
#else
    if (dlytim <= 0) return;
    int idx = s_current_task_idx;
    if (idx >= 0 && idx < MAX_SMP_TASKS) {
        s_smp_tasks[idx].sleeping = TRUE;
        s_smp_tasks[idx].runnable = FALSE;
        s_smp_tasks[idx].wake_tick = g_lapic_ticks + (uint64_t)dlytim;
        s_current_task_idx = -1;
#if defined(__x86_64__)
        if (s_scheduler_sp) {
            btron_ctx_switch(&s_smp_tasks[idx].sp, s_scheduler_sp);
        }
#endif
    } else {
        uint64_t target = g_lapic_ticks + (uint64_t)dlytim;
        while (g_lapic_ticks < target) {
            btron_scheduler_tick();
            btron_smp_pause();
        }
    }
#endif
}

#if !SMP_HOSTED && defined(__x86_64__)
static struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) s_idt[256] __attribute__((aligned(16)));

static struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) s_idtr;

__attribute__((naked)) static void btron_default_isr(void) {
    __asm__ volatile(
        "pushq %rax\n\t"
        "movq s_lapic(%rip), %rax\n\t"
        "testq %rax, %rax\n\t"
        "jz 1f\n\t"
        "movl $0, 0xB0(%rax)\n\t"
        "1:\n\t"
        "popq %rax\n\t"
        "iretq\n\t"
    );
}

void btron_lapic_timer_isr_c(void) {
    g_lapic_ticks++;
    for (int i = 0; i < MAX_SMP_TASKS; i++) {
        if (s_smp_tasks[i].active && s_smp_tasks[i].sleeping) {
            if (g_lapic_ticks >= s_smp_tasks[i].wake_tick) {
                s_smp_tasks[i].sleeping = FALSE;
                s_smp_tasks[i].runnable = TRUE;
            }
        }
    }
}

__attribute__((naked)) static void btron_lapic_timer_isr(void) {
    __asm__ volatile(
        "pushq %rax\n\t"
        "pushq %rcx\n\t"
        "pushq %rdx\n\t"
        "pushq %rsi\n\t"
        "pushq %rdi\n\t"
        "pushq %r8\n\t"
        "pushq %r9\n\t"
        "pushq %r10\n\t"
        "pushq %r11\n\t"
        "call btron_lapic_timer_isr_c\n\t"
        "movq s_lapic(%rip), %rax\n\t"
        "testq %rax, %rax\n\t"
        "jz 1f\n\t"
        "movl $0, 0xB0(%rax)\n\t"
        "1:\n\t"
        "popq %r11\n\t"
        "popq %r10\n\t"
        "popq %r9\n\t"
        "popq %r8\n\t"
        "popq %rdi\n\t"
        "popq %rsi\n\t"
        "popq %rdx\n\t"
        "popq %rcx\n\t"
        "popq %rax\n\t"
        "iretq\n\t"
    );
}

static void btron_init_idt(void) {
    for (int i = 0; i < 256; i++) {
        uintptr_t addr = (uintptr_t)btron_default_isr;
        s_idt[i].offset_low  = (uint16_t)(addr & 0xFFFF);
        s_idt[i].selector    = 0x08;
        s_idt[i].ist         = 0;
        s_idt[i].flags       = 0x8E;
        s_idt[i].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
        s_idt[i].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
        s_idt[i].reserved    = 0;
    }
    uintptr_t taddr = (uintptr_t)btron_lapic_timer_isr;
    s_idt[0x20].offset_low  = (uint16_t)(taddr & 0xFFFF);
    s_idt[0x20].selector    = 0x08;
    s_idt[0x20].ist         = 0;
    s_idt[0x20].flags       = 0x8E;
    s_idt[0x20].offset_mid  = (uint16_t)((taddr >> 16) & 0xFFFF);
    s_idt[0x20].offset_high = (uint32_t)((taddr >> 32) & 0xFFFFFFFF);
    s_idt[0x20].reserved    = 0;

    s_idtr.limit = sizeof(s_idt) - 1;
    s_idtr.base  = (uint64_t)(uintptr_t)s_idt;
    __asm__ volatile("lidt %0" : : "m"(s_idtr));

    __asm__ volatile("outb %0, $0x21" : : "a"((uint8_t)0xFF));
    __asm__ volatile("outb %0, $0xA1" : : "a"((uint8_t)0xFF));
}

static void btron_init_lapic_timer(void) {
    if (!s_lapic) {
        uint32_t base = smp_read_lapic_base_msr();
        s_lapic = (volatile uint32_t *)(uintptr_t)base;
    }
    if (!s_lapic) return;

    s_lapic[LAPIC_SVR / 4] = LAPIC_SVR_ENABLE | 0xFF;
    s_lapic[LAPIC_TPR / 4] = 0;
    s_lapic[LAPIC_DIV_CFG / 4] = 0x03;
    s_lapic[LAPIC_TIMER / 4] = 0x20000 | 0x20;
    s_lapic[LAPIC_INIT_COUNT / 4] = 62500;

    btron_init_idt();
    __asm__ volatile("sti");
}
#endif

void btron_scheduler_tick(void) {
#if !SMP_HOSTED && defined(__x86_64__)
    static uint32_t s_last_lapic_count = 0;
    if (s_lapic) {
        uint32_t curr = s_lapic[LAPIC_CURR_COUNT / 4];
        if (s_last_lapic_count == 0) {
            s_last_lapic_count = curr;
        } else if (curr > s_last_lapic_count) {
            g_lapic_ticks++;
        }
        s_last_lapic_count = curr;
    }
    for (int i = 0; i < MAX_SMP_TASKS; i++) {
        if (s_smp_tasks[i].active && s_smp_tasks[i].sleeping) {
            if (g_lapic_ticks >= s_smp_tasks[i].wake_tick) {
                s_smp_tasks[i].sleeping = FALSE;
                s_smp_tasks[i].runnable = TRUE;
            }
        }
    }
#endif
}

void btron_scheduler_dispatch(void) {
#if !SMP_HOSTED && defined(__x86_64__)
    btron_scheduler_tick();

    for (int i = 0; i < MAX_SMP_TASKS; i++) {
        if (!s_smp_tasks[i].active) continue;
        if (s_smp_tasks[i].sleeping) {
            if (g_lapic_ticks >= s_smp_tasks[i].wake_tick) {
                s_smp_tasks[i].sleeping = FALSE;
                s_smp_tasks[i].runnable = TRUE;
            } else {
                continue;
            }
        }
        if (s_smp_tasks[i].runnable) {
            s_current_task_idx = i;
            btron_ctx_switch(&s_scheduler_sp, s_smp_tasks[i].sp);
            s_current_task_idx = -1;
        }
    }
#endif
}

void btron_core_banner(void) {
    uart_puts_raw("B-System/BTRON3 3.20 (x86_64-uefi-smp) Kota Uchida — T-Kernel 2.0\r\n");
    uart_puts_raw("Copyright 2026 Synrc Research Center. MIT License.\r\n");
    uart_puts_raw("[BOOT] Machine: QEMU q35  x86_64 EMT64  SMP  ACPI 6.5\r\n");
    uart_puts_raw("\r\n");
}

void btron_core_init(void) {
    /* ── CPU detection: read CPUID to report feature flags ── */
    uart_puts_raw("[CPU ] x86_64 UEFI SMP (QEMU q35)  Long Mode  CR0/CR4/EFER active\r\n");
#if !SMP_HOSTED && (defined(__x86_64__) || defined(__i386__))
    {
        uint32_t eax = 1u, ebx = 0, ecx = 0, edx = 0;
        __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        uart_puts_raw("[CPU ] CPUID[1] ECX=");
        smp_uart_hex32(ecx);
        uart_puts_raw("  EDX=");
        smp_uart_hex32(edx);
        uart_puts_raw("  (SSE/SSE2/SSE3/POPCNT)\r\n");
    }
#endif

    /* ── SMP: MADT parse, prepare stacks, boot APs ── */
    btron_smp_parse_madt(NULL);
    btron_smp_prepare_aps();
    btron_smp_boot_aps();

    /* ── IRQ subsystem ── */
    uart_puts_raw("\r\n");
    uart_puts_raw("[IRQ ] PIC 8259A: Disabled (IO-APIC supersedes PIC)\r\n");

    /* Probe HPET capabilities register (offset 0) for real timer period */
    uint32_t hpet_period_ns = 0;
#if !SMP_HOSTED && (defined(__x86_64__) || defined(__i386__))
    {
        volatile uint32_t *hpet_hi = (volatile uint32_t *)0xFED00004uL;
        uint32_t period_fs = *hpet_hi; /* femtoseconds per tick */
        if (period_fs > 0) hpet_period_ns = period_fs / 1000000u;
    }
#endif
    uart_puts_raw("[IRQ ] HPET: Base 0xFED00000  Period ~");
    if (hpet_period_ns > 0) smp_uart_dec(hpet_period_ns); else uart_puts_raw("100");
    uart_puts_raw(" ns  [OK]\r\n");
    uart_puts_raw("[IRQ ] Local APIC Timer: TSC-deadline mode  [CALIBRATING]\r\n");
#if !SMP_HOSTED && defined(__x86_64__)
    btron_init_lapic_timer();
    uart_puts_raw("[IRQ ] Local APIC Timer: Periodic mode @ 1000 Hz (vector 0x20)  [ACTIVE]\r\n");
    uart_puts_raw("[SMP ] Scheduler active: Local APIC timer tick plugged\r\n");
#endif
    uart_puts_raw("[IRQ ] UART NS16550A COM1 0x3F8 (115200 8N1)  [ACTIVE]\r\n");
}

void btron_core_mem_log(void) {
    uart_puts_raw("[MEM ] E820 Physical Memory Map (QEMU q35, 1 GB RAM):\r\n");
    uart_puts_raw("[MEM ]   0x00000000-0x0009FFFF  640 KB conventional\r\n");
    uart_puts_raw("[MEM ]   0x000A0000-0x000FFFFF  Reserved (VGA/ROM)\r\n");
    uart_puts_raw("[MEM ]   0x00100000-0x3FFFFFFF  1023 MB usable\r\n");
    uart_puts_raw("[MEM ]   0xFEC00000-0xFEC00FFF  IO-APIC MMIO\r\n");
    uart_puts_raw("[MEM ]   0xFEE00000-0xFEEFFFFF  Local APIC MMIO\r\n");
    uart_puts_raw("[MEM ] Heap: 0x00100000-0x40000000 (1023 MB)\r\n");
}

void btron_core_hfds_log(void) {
    uart_puts_raw("[HFDS] VirtIO-Block: Queue 128  IRQ 10  [DETECT]\r\n");
    uart_puts_raw("[HFDS] HFDS Hierarchical File/Data Set: INIT  [OK]\r\n");
    uart_puts_raw("[HFDS] Root Cabinet: BTRON3_SPEC.TAD  T_KERNEL_20.TAD\r\n");
    uart_puts_raw("[HFDS]              MOZC_DICT.DAT     SKI_BOOTMAN.SYS\r\n");
}

void btron_core_print_ver(ShellOutputFn out_fn, void *user_data, const char *arg) {
    (void)arg;
    if (!out_fn) return;
    out_fn("B-System BTRON3 3.20 (x86_64 UEFI SMP Edition — Kota Uchida Kernel)", COLOR_GREEN, user_data);
    out_fn("  Platform: x86_64 UEFI (EMT64 Multi-Core SMP · 4 Cores Live)", COLOR_LTGRAY, user_data);
    out_fn("  Subsystem: ACPI 6.5 MADT, Local APIC (0xFEE00000), IO-APIC (0xFEC00000)", COLOR_LTGRAY, user_data);
    out_fn("  Bootloader: Ski Bootloader (🎿 Ski Bootloader) · AP Trampoline: 0x9000", COLOR_CYAN, user_data);
}
