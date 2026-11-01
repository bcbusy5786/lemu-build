/*
 * QEMU MIPS timer support
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "qemu/timer.h"
#include "internal.h"

/* MIPS R4K timer */
static uint32_t cpu_mips_get_count_val(CPUMIPSState *env)
{
    MIPSCPU *cpu = env_archcpu(env);
    int64_t now_ns;
    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    return env->CP0_Count +
            (uint32_t)clock_ns_to_ticks(cpu->count_clock, now_ns);
}

static void cpu_mips_timer_update(CPUMIPSState *env)
{
    MIPSCPU *cpu = env_archcpu(env);
    uint64_t now_ns, next_ns;
    uint32_t wait;

    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    wait = env->CP0_Compare - cpu_mips_get_count_val(env);
    /* Clamp interval to overflow if virtual time had not progressed */
    if (!wait) {
        wait = UINT32_MAX;
    }
    next_ns = now_ns + clock_ticks_to_ns(cpu->count_clock, wait);
    timer_mod(env->timer, next_ns);
}

/* Expire the timer.  */
static void cpu_mips_timer_expire(CPUMIPSState *env)
{
    cpu_mips_timer_update(env);
    if (env->insn_flags & ISA_MIPS_R2) {
        env->CP0_Cause |= 1 << CP0Ca_TI;
    }
    qemu_irq_raise(env->irq[(env->CP0_IntCtl >> CP0IntCtl_IPTI) & 0x7]);
}

uint32_t cpu_mips_get_count(CPUMIPSState *env)
{
    if (env->CP0_Cause & (1 << CP0Ca_DC)) {
        env->count_read = env->CP0_Count;
    } else {
        uint64_t now_ns;

        now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (timer_pending(env->timer)
            && timer_expired(env->timer, now_ns)) {
            /* The timer has already expired.  */
            cpu_mips_timer_expire(env);
        }

        env->count_read = cpu_mips_get_count_val(env);
    }
    return env->count_read;
}

void cpu_mips_store_count(CPUMIPSState *env, uint32_t count)
{
    /*
     * This gets called from cpu_state_reset(), potentially before timer init.
     * So env->timer may be NULL, so treat timer as disabled in that case.
     */
    MIPSCPU *cpu = env_archcpu(env);
    if (env->CP0_Cause & (1 << CP0Ca_DC) || !env->timer) {
        env->CP0_Count = count;
    } else {
        /* Store new count register */
        env->CP0_Count = count - (uint32_t)clock_ns_to_ticks(cpu->count_clock,
                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        /* Update timer timer */
        cpu_mips_timer_update(env);
    }
}

/*
 * SINIX reads Count, and when it is within 3 ticks of Compare it writes
 * Count + 48 to Compare, so that the interrupt is not lost.  A real CPU
 * writes it a few cycles later, but TCG can take microseconds, so Count
 * may already be past the new value, and the interrupt then comes only
 * after Count wraps, 64 s later.  With late-compare, a value set ahead
 * of the last Count the guest read, which Count has passed since,
 * matches at once.  A value behind that Count, such as Count - 1 written
 * to stop the timer, waits for the wrap as before.
 */
#define LATE_COMPARE_WINDOW (1u << 26) /* about 1 s at 66 MHz */

static bool cpu_mips_compare_is_late(CPUMIPSState *env, uint32_t value)
{
    uint32_t ahead, passed;

    if (!env_archcpu(env)->late_compare ||
        (env->CP0_Cause & (1 << CP0Ca_DC))) {
        return false;
    }
    ahead = value - env->count_read;
    passed = cpu_mips_get_count_val(env) - env->count_read;
    return ahead && ahead <= passed && passed <= LATE_COMPARE_WINDOW;
}

void cpu_mips_store_compare(CPUMIPSState *env, uint32_t value)
{
    bool late = cpu_mips_compare_is_late(env, value);

    env->CP0_Compare = value;
    if (!(env->CP0_Cause & (1 << CP0Ca_DC))) {
        cpu_mips_timer_update(env);
    }
    if (env->insn_flags & ISA_MIPS_R2) {
        env->CP0_Cause &= ~(1 << CP0Ca_TI);
    }
    qemu_irq_lower(env->irq[(env->CP0_IntCtl >> CP0IntCtl_IPTI) & 0x7]);
    if (late) {
        cpu_mips_timer_expire(env);
    }
}

void cpu_mips_start_count(CPUMIPSState *env)
{
    cpu_mips_store_count(env, env->CP0_Count);
}

void cpu_mips_stop_count(CPUMIPSState *env)
{
    /* Store the current value */
    MIPSCPU *cpu = env_archcpu(env);
    env->CP0_Count += (uint32_t)clock_ns_to_ticks(cpu->count_clock,
                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
}

static void mips_timer_cb(void *opaque)
{
    CPUMIPSState *env;

    env = opaque;

    if (env->CP0_Cause & (1 << CP0Ca_DC)) {
        return;
    }

    cpu_mips_timer_expire(env);
}

void cpu_mips_clock_init(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;

    env->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &mips_timer_cb, env);
}
