#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"
#include "config.h"

#ifdef ENABLE_STACKCHECK

// Get segment base and limit (in bytes) from LDT descriptor
PRIVATE void get_seg_info(struct descriptor *d, u32 *base, u32 *limit_bytes)
{
    u32 b = ((u32)d->base_high << 24) | ((u32)d->base_mid << 16) | d->base_low;

    u32 limit_raw = ((u32)(d->limit_high_attr2 & 0x0F) << 16) | d->limit_low;

    u32 bytes;
    if (d->limit_high_attr2 & (DA_LIMIT_4K >> 8))
    {
        bytes = (limit_raw + 1) << 12;
    }
    else
    {
        bytes = limit_raw + 1;
    }

    *base = b;
    *limit_bytes = bytes;
}

// Determine process type
PRIVATE int get_proc_type(int pid)
{
    if (pid < NR_TASKS)
    {
        return 0;
    }
    else if (pid < NR_TASKS + NR_NATIVE_PROCS)
    {
        return 1;
    }
    else
    {
        return 2;
    }
}

// Get process type name for logging
PRIVATE const char *get_proc_type_name(int pid)
{
    int type = get_proc_type(pid);
    switch (type)
    {
    case 0:
        return "TASK";
    case 1:
        return "NATIVE";
    case 2:
        return "USER";
    default:
        return "UNKNOWN";
    }
}

PRIVATE void get_stack_bounds(struct proc *p, int pid, u32 *low, u32 *high, int *is_user)
{
    int proc_type = get_proc_type(pid);

    if (proc_type == 2)
    {
        *is_user = 1;

        u32 seg_base, seg_limit;
        get_seg_info(&p->ldts[INDEX_LDT_RW], &seg_base, &seg_limit);

        // u32 esp_la = seg_base + p->regs.esp;

        // /* stack_high: top (exclusive); stack_low: lowest ESP ever seen (low-water mark) */
        // p->stack_high = seg_base + seg_limit;
        // if (p->stack_low < seg_base || p->stack_low >= p->stack_high)
        // {
        //     p->stack_low = esp_la;
        // }
        if (p->regs.eip < p->stack_low)
        {
            p->stack_low = p->regs.eip;
        }

        *low = p->stack_low;
        *high = p->stack_high;
    }
    else
    {
        *is_user = 0;

        *low = p->stack_low;
        *high = p->stack_high;
    }
}

PRIVATE int is_ret_valid_task_native(u32 ret_la)
{
    // 栈不可执行（NX）
    if (ret_la >= task_stack && ret_la < task_stack + STACK_SIZE_TOTAL)
    {
        return 0;
    }

    return 1;
}

// Validate return address for USER (fork/exec) processes
PRIVATE int is_ret_valid_user(struct proc *p, u32 ret_off, u32 esp_off, u32 seg_limit)
{
    if (ret_off >= seg_limit)
    {
        return 0;
    }

    if (ret_off >= esp_off && ret_off < seg_limit)
    {
        return 0;
    }

    return 1;
}

PUBLIC void stackcheck_proc(struct proc *p)
{
    int pid = proc2pid(p);

    if (pid < 0 || pid >= NR_TASKS + NR_PROCS)
    {
        return;
    }

    if (p->p_flags != 0)
    {
        return;
    }

    u32 stack_low, stack_high;
    int is_user;
    get_stack_bounds(p, pid, &stack_low, &stack_high, &is_user);

    u32 seg_base = 0, seg_limit = 0;

    get_seg_info(&p->ldts[INDEX_LDT_RW], &seg_base, &seg_limit);
    if (is_user)
    {
        get_seg_info(&p->ldts[INDEX_LDT_RW], &seg_base, &seg_limit);

        // 栈NX：EIP 进入“历史栈范围”直接判定ret2stack
        // u32 eip_la = seg_base + p->regs.eip;
        // if (eip_la >= stack_low && eip_la < stack_high)
        if (p->regs.eip >= stack_low && p->regs.eip < stack_high)
        {
            panic("[STACKNX] USER pid=%d name=%s: eip=0x%x in stack [0x%x,0x%x)\n",
                  pid, p->name, p->regs.eip, stack_low, stack_high);
            return;
        }
    }

    u32 ebp_off = p->regs.ebp;
    u32 esp_off = p->regs.esp;

    if (ebp_off == 0)
    {
        return;
    }

    int frame_count = 0;
    const int MAX_FRAMES = STACKCHECK_MAX_FRAMES;

    while (frame_count < MAX_FRAMES)
    {
        u32 ebp_la;

        if (is_user)
        {
            ebp_la = seg_base + ebp_off;
        }
        else
        {
            ebp_la = ebp_off;
        }

        u32 next_ebp_off = *(u32 *)(ebp_la);
        if (next_ebp_off == 0)
        {
            return;
        }

        u32 ret_addr_off = *(u32 *)(ebp_la + 4);

        int ret_valid;
        if (is_user)
        {
            ret_valid = is_ret_valid_user(p, ret_addr_off, esp_off, seg_limit);
        }
        else
        {
            ret_valid = is_ret_valid_task_native(ret_addr_off);
        }

        if (next_ebp_off <= ebp_off)
        {
            ret_valid = 0;
        }

        if (!ret_valid)
        {
            panic("[STACKCHK] %s pid=%d name=%s frame=%d: INVALID ret=0x%x at ebp=0x%x\n",
                  get_proc_type_name(pid), pid, p->name, frame_count, ret_addr_off, ebp_la);
            return;
        }

        ebp_off = next_ebp_off;
        frame_count++;
    }

    printl("[STACKCHK] %s pid=%d name=%s: reached max frames (%d)\n",
           get_proc_type_name(pid), pid, p->name, MAX_FRAMES);
}

PUBLIC void stackcheck_on_tick()
{
    static int last_check_tick = -1;

    if (last_check_tick < 0)
    {
        last_check_tick = ticks;
        return;
    }

    int elapsed;
    if (ticks >= last_check_tick)
    {
        elapsed = ticks - last_check_tick;
    }
    else
    {
        elapsed = (MAX_TICKS - last_check_tick) + ticks;
    }

    if (elapsed < STACKCHECK_INTERVAL_TICKS)
    {
        return;
    }

    last_check_tick = ticks;

    if (p_proc_ready != 0)
    {
        printl("[CHECK] Validity check started...\n");
        stackcheck_proc(p_proc_ready);
        printl("[CHECK] Validity check finished...\n");
    }
}

#endif // ENABLE_STACKCHECK
