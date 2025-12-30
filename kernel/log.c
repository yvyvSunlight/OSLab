/*
@Author  : Ramoor
@Date    : 2025-12-29
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

/* -------------------- 可配置项 -------------------- */
#define MM_LOG_PATH        "/mm.log"
#define MM_LOG_BUF_SIZE    (8 * 1024)
#define MM_LOG_LINE_MAX    192

/* -------------------- RTC 时间 -------------------- */
static int g_mm_log_enabled = 1;

static int read_register_log(char reg_addr)
{
    out_byte(CLK_ELE, reg_addr);
    return in_byte(CLK_IO);
}

static int get_rtc_time_log(struct time *t)
{
    t->year   = read_register_log(YEAR);
    t->month  = read_register_log(MONTH);
    t->day    = read_register_log(DAY);
    t->hour   = read_register_log(HOUR);
    t->minute = read_register_log(MINUTE);
    t->second = read_register_log(SECOND);

    if ((read_register_log(CLK_STATUS) & 0x04) == 0) {
        t->year   = BCD_TO_DEC(t->year);
        t->month  = BCD_TO_DEC(t->month);
        t->day    = BCD_TO_DEC(t->day);
        t->hour   = BCD_TO_DEC(t->hour);
        t->minute = BCD_TO_DEC(t->minute);
        t->second = BCD_TO_DEC(t->second);
    }

    t->year += 2000;
    return 0;
}

/* -------------------- 写文件落盘 -------------------- */
static int  g_mm_log_fd = -1;
static char g_mm_log_buf[MM_LOG_BUF_SIZE];
static int  g_mm_log_buf_len = 0;

static void mm_log_try_open(void)
{
    if (g_mm_log_fd >= 0)
        return;

    /* 不使用 O_APPEND/O_WRONLY，避免未定义 */
    int flags = O_CREAT | O_RDWR;

    /*
     * 若你的 open 是 3 参数：open(path, flags, mode)，就改成：
     * g_mm_log_fd = open(MM_LOG_PATH, flags, 0644);
     */
    g_mm_log_fd = open(MM_LOG_PATH, flags);

    if (g_mm_log_fd < 0)
        return;

    /* 打开成功：先把文件指针移到末尾，模拟 append */
    lseek(g_mm_log_fd, 0, SEEK_END);

    /* flush buffer */
    if (g_mm_log_buf_len > 0) {
        write(g_mm_log_fd, g_mm_log_buf, g_mm_log_buf_len);
        g_mm_log_buf_len = 0;
    }
}

static void mm_log_write_bytes(const char* s, int len)
{
    if (len <= 0 || !s)
        return;

    mm_log_try_open();

    if (g_mm_log_fd >= 0) {
        /* 每次写前都挪到末尾，保证追加 */
        lseek(g_mm_log_fd, 0, SEEK_END);
        write(g_mm_log_fd, (void*)s, len);
        return;
    }

    /* 文件不可用：写入内存缓冲 */
    if (g_mm_log_buf_len + len > MM_LOG_BUF_SIZE) {
        /* 缓冲满：丢弃新日志（也可以选择覆盖旧的，看你需求） */
        return;
    }

    memcpy(g_mm_log_buf + g_mm_log_buf_len, (void*)s, len); /* 强转消除 const 警告 */
    g_mm_log_buf_len += len;
}

void log_mm_flush(void)
{
    mm_log_try_open();
    if (g_mm_log_fd >= 0 && g_mm_log_buf_len > 0) {
        lseek(g_mm_log_fd, 0, SEEK_END);
        write(g_mm_log_fd, g_mm_log_buf, g_mm_log_buf_len);
        g_mm_log_buf_len = 0;
    }
}

void log_mm_close(void)
{
    if (g_mm_log_fd >= 0) {
        close(g_mm_log_fd);
        g_mm_log_fd = -1;
    }
}

/* mm log */
void log_mm_enable(int enable)
{
    g_mm_log_enabled = (enable != 0);
}

static const char* mm_type_name(int msgtype)
{
    switch (msgtype) {
    case FORK: return "FORK";
    case EXIT: return "EXIT";
    case EXEC: return "EXEC";
    case WAIT: return "WAIT";
    case KILL: return "KILL";
    default:   return "UNKNOWN";
    }
}

void log_mm_event(int msgtype, int src, int val)
{
    if (!g_mm_log_enabled)
        return;

    struct time t;
    get_rtc_time_log(&t);

    char line[MM_LOG_LINE_MAX];
    int n;

    if (val == -1) {
        n = sprintf(line,
                    "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] MM_%s\n",
                    t.year, t.month, t.day,
                    t.hour, t.minute, t.second,
                    src, mm_type_name(msgtype));
    } else {
        n = sprintf(line,
                    "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] MM_%s val=%d\n",
                    t.year, t.month, t.day,
                    t.hour, t.minute, t.second,
                    src, mm_type_name(msgtype), val);
    }

    printl("%s", line);
    mm_log_write_bytes(line, n);
}
