/*
@Author  : Ramoor
@Date    : 2025-12-30
@Update  : 2026-01-02
*/

#include "type.h"
#include "config.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"
#include "log.h"

/* x86 下 pause 是非特权指令：降低忙等对CPU/虚拟机的压力 */
static inline void cpu_relax(void)
{
    __asm__ __volatile__("pause");
}

/* 仅用于启动初期等待（一次性），保持原行为 */
static void delay_ms_light(int ms)
{
    int dt = (ms * HZ) / 1000;
    if (dt < 1) dt = 1;

    int target = ticks + dt;

    while ((int)(ticks - target) < 0) {
        int cur = ticks;
        while (ticks == cur && (int)(ticks - target) < 0) {
            cpu_relax();
        }
    }
}

PUBLIC void task_log(void)
{
    MESSAGE msg;

    /* 给 FS/HD 初始化留时间：保持原行为 */
    delay_ms_light(2000);

    while (1) {
        // 如果没有 flush 请求，就阻塞睡眠等待消息唤醒
        if (!log_fetch_and_clear_flush_req()) {
            reset_msg(&msg);
            send_recv(RECEIVE, ANY, &msg);
            /* 醒来后再清一次，合并多次 kick */
            (void)log_fetch_and_clear_flush_req();
        }

        /* flush 四类日志（flush_common 内部会判空） */
        log_mm_flush();
        log_sys_flush();
        log_fs_flush();
        log_hd_flush();
    }
}