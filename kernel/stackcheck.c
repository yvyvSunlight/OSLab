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

PRIVATE int is_user(int pid)
{
    return (pid >= NR_TASKS + NR_NATIVE_PROCS);
}

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

PRIVATE void stack_nx_user(struct proc *p, int pid)
{
    // 栈NX：EIP 进入“历史栈范围”直接判定ret2stack
    if (p->regs.eip >= p->stack_low && p->regs.eip < p->stack_high)
    {
        panic("[STACK NX] USER pid=%d name=%s: INVALID eip=0x%x in stack [0x%x,0x%x)\n",
              pid, p->name, p->regs.eip, p->stack_low, p->stack_high);
        return;
    }
}

PRIVATE void stack_nx_task_native(struct proc *p, int pid)
{
    if (p->regs.eip >= task_stack && p->regs.eip < task_stack + STACK_SIZE_TOTAL)
    {
        panic("[STACK NX] TASK/NATIVE pid=%d name=%s: INVALID eip=0x%x in stack [0x%x,0x%x)\n",
              pid, p->name, p->regs.eip, task_stack, task_stack + STACK_SIZE_TOTAL);
        return;
    }
}

PRIVATE void retaddr_check_user(struct proc *p, int pid)
{
    u32 ebp_la;
    u32 ret_addr;
    u32 ebp_off = p->regs.ebp;

    if (ebp_off == 0)
    {
        return;
    }
    u32 seg_base, seg_limit;
    get_seg_info(&p->ldts[INDEX_LDT_RW], &seg_base, &seg_limit);

    int frame_count = 0;
    while (frame_count < STACKCHECK_MAX_FRAMES)
    {
        ebp_la = seg_base + ebp_off;
        // 下一个ebp
        ebp_off = *(u32*)(ebp_la);
        if(ebp_off == 0)
        {
            return;
        }
        ret_addr = *(u32*)(ebp_la + 4);
        if(ret_addr >= seg_limit || (ret_addr >= p->stack_low && ret_addr < p->stack_high))
        {
            panic("[RETADDR CHECK] USER pid=%d name=%s frame=%d: INVALID ret_addr=0x%x in stack [0x%x,0x%x)\n",
                  pid, p->name, frame_count, ret_addr, p->stack_low, p->stack_high);
            return;
        }
        frame_count++;
    }
}

PRIVATE void retaddr_check_task_native(struct proc *p, int pid)
{
    u32 ret_addr;
    u32 ebp_off = p->regs.ebp;

    // 初始ebp和p->stack_high相等
    if (ebp_off == p->stack_high)
    {
        return;
    }

    int frame_count = 0;
    while (frame_count < STACKCHECK_MAX_FRAMES)
    {
        // 下一个ebp
        ebp_off = *(u32 *)(ebp_off);
        if (ebp_off == p->stack_high)
        {
            return;
        }
        ret_addr = *(u32 *)(ebp_off + 4);
        if (ret_addr >= task_stack && ret_addr < task_stack + STACK_SIZE_TOTAL)
        {
            panic("[RETADDR CHECK] TASK/NATIVE pid=%d name=%s frame=%d: INVALID ret_addr=0x%x in stack [0x%x,0x%x)\n",
                  pid, p->name, frame_count, ret_addr, task_stack, task_stack + STACK_SIZE_TOTAL);
            return;
        }
        frame_count++;
    }
}

PUBLIC void stackcheck_proc(struct proc *p)
{
    int pid = proc2pid(p);
    if(is_user(pid))
    {
        if (p->regs.esp < p->stack_low)
        {
            p->stack_low = p->regs.esp; // 更新最低栈顶
        }
        stack_nx_user(p, pid);
        retaddr_check_user(p, pid);
    }
    else
    {
        stack_nx_task_native(p, pid);
        retaddr_check_task_native(p, pid);
    }
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
        printl("[STACK CHECK] Validity check started...\n");
        stackcheck_proc(p_proc_ready);
        printl("[STACK CHECK] Validity check finished...\n");
    }
}

#endif // ENABLE_STACKCHECK