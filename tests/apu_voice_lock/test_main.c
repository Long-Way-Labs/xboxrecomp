/* NV1BA0_PIO_VOICE_LOCK, and VOICE_ON/RELEASE, as posted writes.
 *
 * voice_lock used to take d->lock, which the APU frame thread holds for the
 * whole of each se_frame, and signal d->cond, which the frame thread's
 * pacing wait sleeps on. A title's VOICE_LOCK therefore waited out the
 * current frame (most of the APU's MMIO handler cost measured under Proton)
 * and woke the pacing wait for nothing. On the MCPX the method is posted
 * into the front end's FIFO and the CPU never waits.
 *
 * The model comes up as tests/apu_lifecycle brings it up: a small zeroed
 * buffer stands in for RAM, every xa2_* is faked, and the frame thread
 * idles on d->cond after init (pause_requested), so it is a second waiter
 * on the cond case 3 watches. VPVADDR puts the voice array in that RAM. */
#include "apu_state.h"
#include "apu.h"
#include "apu_xaudio2.h"
#include "apu_regs.h"
#include "platform/host_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The runtime links against the title's dispatch; nothing here calls it. */
void *recomp_lookup(unsigned long address) { (void)address; abort(); }
void *recomp_lookup_manual(unsigned long address) { (void)address; abort(); }

/* ---- fake device: every xa2_* symbol, so apu_xaudio2.o is not linked ---- */
int  xa2_init(void)            { return 1; }
void xa2_shutdown(void)        {}
int  xa2_is_active(void)       { return 0; }
int  xa2_get_buffer_size(void) { return 512; }
int  xa2_get_buffer_count(void) { return 4; }
unsigned xa2_underruns(void)   { return 0; }
uint64_t xa2_samples_played(void) { return 0; }
int  xa2_queued_samples(void)  { return 0; }
void xa2_expect_drain(void)    {}
int  xa2_submit_samples(const int16_t *s, int n) { (void)s; (void)n; return 1; }

#define FAKE_RAM   (256u << 10)
#define VOICES     0x00030000u      /* 256 voices x NV_PAVS_SIZE, 32 KB */
#define VP_BASE    0x20000u

static int failures;
static MCPXAPUState *d;

static void check(int ok, const char *what)
{
    printf("  %-66s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static double now_ms(void)
{
    return (double)xbox_HostNowNs() / 1e6;
}

static void method(uint32_t m, uint32_t arg)
{
    mcpx_apu_mmio_write(d, VP_BASE + m, arg, 4);
}

static int locked(unsigned v)
{
    return (int)((d->vp.voice_locked[v / 64] >> (v % 64)) & 1);
}

/* ---- case 1: no method waits while the frame lock is held ---- */

static volatile LONG holder_has_lock;

static DWORD WINAPI hold_lock(LPVOID arg)
{
    (void)arg;
    qemu_mutex_lock(&d->lock);
    holder_has_lock = 1;
    xbox_HostSleepNs(200ull * 1000 * 1000);
    qemu_mutex_unlock(&d->lock);
    return 0;
}

/* Returns the method's duration in ms. */
static double timed(uint32_t m, uint32_t arg)
{
    double t0 = now_ms();
    method(m, arg);
    return now_ms() - t0;
}

static void no_wait_case(void)
{
    HANDLE h = CreateThread(NULL, 0, hold_lock, NULL, 0, NULL);
    double worst = 0, t;

    while (!holder_has_lock)
        xbox_HostSleepNs(100 * 1000);

    /* Voice 9 is the title's: it locks it and keeps it locked. VOICE_ON on
     * voice 7 then takes and drops 7's bit only. INHERIT with antecedent
     * voice 0 keeps the list registers out of it. */
    method(NV1BA0_PIO_SET_ANTECEDENT_VOICE, 0);
    t = timed(NV1BA0_PIO_SET_CURRENT_VOICE, 9);      if (t > worst) worst = t;
    t = timed(NV1BA0_PIO_VOICE_LOCK, 1);             if (t > worst) worst = t;
    check(locked(9), "VOICE_LOCK(1) sets the current voice's bit");
    t = timed(NV1BA0_PIO_SET_CURRENT_VOICE, 7);      if (t > worst) worst = t;
    t = timed(NV1BA0_PIO_VOICE_LOCK, 1);             if (t > worst) worst = t;
    check(locked(7), "VOICE_LOCK(1) on a second voice sets its bit");
    t = timed(NV1BA0_PIO_VOICE_LOCK, 0);             if (t > worst) worst = t;
    check(!locked(7) && locked(9), "VOICE_LOCK(0) clears only the current voice's bit");
    t = timed(NV1BA0_PIO_VOICE_ON, 7);               if (t > worst) worst = t;
    check(locked(9) && !locked(7), "VOICE_ON leaves the title's lock and clears its own");
    t = timed(NV1BA0_PIO_VOICE_RELEASE, 7);          if (t > worst) worst = t;
    check(locked(9) && !locked(7), "VOICE_RELEASE leaves the title's lock and clears its own");
    t = timed(NV1BA0_PIO_SET_CURRENT_VOICE, 9);      if (t > worst) worst = t;
    t = timed(NV1BA0_PIO_VOICE_LOCK, 0);             if (t > worst) worst = t;
    check(!locked(9), "VOICE_LOCK(0) clears the title's bit");

    printf("  slowest method with d->lock held elsewhere: %.3f ms\n", worst);
    check(worst < 20.0, "no method waits for the frame lock (each < 20 ms)");

    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
}

/* ---- case 2: two voices share a word; no update is lost ----
 *
 * Run on voices 3 and 5 and again on 35 and 37: the high half of the 64-bit
 * word is what a 32-bit interlocked op (qatomic_or on a long under MSVC)
 * would drop. */

#define TOGGLES 1000000

static DWORD WINAPI toggle_voice(LPVOID arg)
{
    /* SET_CURRENT_VOICE is a shared register, so each thread uses VOICE_ON's
     * own take-and-drop on its handle: the same read-modify-write as
     * VOICE_LOCK, on a voice the thread names itself. */
    uint32_t v = (uint32_t)(uintptr_t)arg;
    for (int i = 0; i < TOGGLES; i++)
        method(NV1BA0_PIO_VOICE_RELEASE, v);
    return 0;
}

static void lost_update_case(unsigned va, unsigned vb)
{
    char what[96];

    /* Each RELEASE sets its voice's bit and clears it again. A racing RMW on
     * the shared word would leave the other voice's bit stuck set (or
     * cleared under a real VOICE_LOCK, which the final check covers). */
    HANDLE a = CreateThread(NULL, 0, toggle_voice, (LPVOID)(uintptr_t)va, 0, NULL);
    HANDLE b = CreateThread(NULL, 0, toggle_voice, (LPVOID)(uintptr_t)vb, 0, NULL);
    WaitForSingleObject(a, INFINITE);
    WaitForSingleObject(b, INFINITE);
    CloseHandle(a);
    CloseHandle(b);
    snprintf(what, sizeof(what), "1M take/drop each on voices %u and %u: both bits clear",
             va, vb);
    check(d->vp.voice_locked[0] == 0, what);

    method(NV1BA0_PIO_SET_CURRENT_VOICE, va);
    method(NV1BA0_PIO_VOICE_LOCK, 1);
    method(NV1BA0_PIO_SET_CURRENT_VOICE, vb);
    method(NV1BA0_PIO_VOICE_LOCK, 1);
    snprintf(what, sizeof(what), "a final lock of each leaves exactly bits %u and %u set",
             va, vb);
    check(d->vp.voice_locked[0] == ((1ull << va) | (1ull << vb)), what);
    method(NV1BA0_PIO_VOICE_LOCK, 0);
    method(NV1BA0_PIO_SET_CURRENT_VOICE, va);
    method(NV1BA0_PIO_VOICE_LOCK, 0);
    check(d->vp.voice_locked[0] == 0, "and unlocking both clears the word");
}

/* VOICE_LOCK on voice 40 sets and clears bit 40 of word 0, and nothing
 * else: a 32-bit op would miss the bit or touch bit 8. */
static void high_bit_case(void)
{
    method(NV1BA0_PIO_SET_CURRENT_VOICE, 40);
    method(NV1BA0_PIO_VOICE_LOCK, 1);
    check(d->vp.voice_locked[0] == (1ull << 40) && locked(40),
          "VOICE_LOCK(1) on voice 40 sets bit 40 of word 0 only");
    method(NV1BA0_PIO_VOICE_LOCK, 0);
    check(d->vp.voice_locked[0] == 0 && !locked(40),
          "VOICE_LOCK(0) on voice 40 clears it");
}

/* ---- case 3: no method wakes a waiter on d->cond ---- */

static volatile LONG waiter_ready, waiter_done;
static int early_returns;

static DWORD WINAPI cond_waiter(LPVOID arg)
{
    (void)arg;
    qemu_mutex_lock(&d->lock);
    waiter_ready = 1;
    double start = now_ms();
    for (;;) {
        double left = 300.0 - (now_ms() - start);
        if (left <= 0)
            break;
        qemu_cond_timedwait(&d->cond, &d->lock, (int)left + 1);
        /* Woken before the time the wait was given: a signal, or a
         * spurious wakeup. */
        if (now_ms() - start < 300.0 - 1.0)
            early_returns++;
    }
    qemu_mutex_unlock(&d->lock);
    waiter_done = 1;
    return 0;
}

static void no_wake_case(void)
{
    HANDLE h = CreateThread(NULL, 0, cond_waiter, NULL, 0, NULL);
    while (!waiter_ready)
        xbox_HostSleepNs(100 * 1000);

    method(NV1BA0_PIO_SET_CURRENT_VOICE, 11);
    for (int i = 0; i < 1000 && !waiter_done; i++) {
        method(NV1BA0_PIO_VOICE_LOCK, i & 1 ? 0 : 1);
        xbox_HostSleepNs(100 * 1000);
    }
    method(NV1BA0_PIO_VOICE_LOCK, 0);

    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    printf("  early returns from a 300 ms wait on d->cond: %d\n", early_returns);
    check(early_returns < 10, "VOICE_LOCK does not signal d->cond (< 10 early returns)");
}

int main(void)
{
    uint8_t *ram = calloc(1, FAKE_RAM);
    if (!ram) return 1;

    printf("apu_voice_lock:\n");
    d = mcpx_apu_init_standalone(ram);
    if (!d) { printf("  init failed\n"); return 1; }
    d->regs[NV_PAPU_VPVADDR] = VOICES;

    no_wait_case();
    lost_update_case(3, 5);
    lost_update_case(35, 37);
    high_bit_case();
    no_wake_case();

    mcpx_apu_shutdown(d);
    free(ram);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
