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

static inline void cpu_relax(void)
{
    __asm__ __volatile__("pause");
}

// 仅用于启动初期等待，保持原行为
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

    // 初始给 FS/HD 初始化留时间
    delay_ms_light(2000);

    while (1) {
        // 如果没有 flush 请求，就阻塞睡眠等待消息唤醒
        if (!log_fetch_and_clear_flush_req()) {
            reset_msg(&msg);
            send_recv(RECEIVE, ANY, &msg);

            (void)log_fetch_and_clear_flush_req();
        }

        // flush 四类日志，写入文件
        log_mm_flush();
        log_sys_flush();
        log_fs_flush();
        log_hd_flush();
    }
}