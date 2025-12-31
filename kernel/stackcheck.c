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

// Get the linear address range of task_stack (for TASK/NATIVE processes)
PRIVATE void get_task_stack_range(u32 *low, u32 *high)
{
    *low = (u32)task_stack;
    *high = (u32)(task_stack + STACK_SIZE_TOTAL);
}

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

// Get kernel text range (for return address validation of TASK/NATIVE)
PRIVATE void get_kernel_text_range(u32 *begin, u32 *end)
{
    static int inited = 0;
    static u32 k_base = 0, k_limit = 0;

    if (!inited)
    {
        unsigned int b = 0, l = 0;
        if (get_kernel_map(&b, &l) == 0)
        {
            k_base = (u32)b;
            k_limit = (u32)l;
        }
        inited = 1;
    }

    *begin = k_base;
    *end = k_base + k_limit;
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

        *low = seg_base;
        *high = seg_base + seg_limit;
    }
    else
    {
        *is_user = 0;

        // if (p->stack_low != 0 && p->stack_high != 0 && p->stack_low < p->stack_high)
        // {
        *low = p->stack_low;
        *high = p->stack_high;
        // }
        // else
        // {
        //     get_task_stack_range(low, high);
        // }
    }
}

PRIVATE int is_ret_valid_task_native(u32 ret_la)
{
    u32 task_stack_low, task_stack_high;
    get_task_stack_range(&task_stack_low, &task_stack_high);

    // 栈不可执行（NX）
    if (ret_la >= task_stack_low && ret_la < task_stack_high)
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
    if (is_user)
    {
        get_seg_info(&p->ldts[INDEX_LDT_RW], &seg_base, &seg_limit);
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
            printl("[STACKCHK] %s pid=%d name=%s frame=%d: INVALID ret=0x%x at ebp=0x%x\n",
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
        printl("[CHECK] Validity check finished...\n ");
    }
}

#endif // ENABLE_STACKCHECK
