/*
@Author  : Ramoor
@Date    : 2025-12-30
@Update  : 2026-01-01
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

static void delay_ms_light(int ms)
{
    int dt = (ms * HZ) / 1000;
    if (dt < 1) dt = 1;

    /* 用有符号差值写法，避免 ticks 将来溢出时逻辑出错 */
    int target = ticks + dt;

    while ((int)(ticks - target) < 0) {
        int cur = ticks;
        /* 等待下一个 tick 到来 */
        while (ticks == cur && (int)(ticks - target) < 0) {
            cpu_relax();
        }
    }
}

PUBLIC void task_log(void)
{
    /* 给 FS/HD 初始化留时间：保持原行为 */
    delay_ms_light(2000);

    while (1) {
        /* 保持原行为：每轮都 flush 四类日志 */
        log_mm_flush();
        log_sys_flush();
        log_fs_flush();
        log_hd_flush();

        /* 保持原行为：500ms 一次 */
        delay_ms_light(500);
    }
}