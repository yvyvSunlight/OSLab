/*
@Author  : Ramoor
@Date    : 2025-12-30
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

static void delay_ms_light(int ms)
{
    int start = ticks;
    int dt = (ms * HZ) / 1000;
    if (dt < 1) dt = 1;
    while ((ticks - start) < dt) { /* busy wait, but no IPC flood */ }
}

PUBLIC void task_log(void)
{
    delay_ms_light(2000);

    while (1) {
        log_mm_flush();
        log_sys_flush();
        log_fs_flush();
        log_hd_flush();

        delay_ms_light(500);
    }
}