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
#include "log.h"

/* -------------------- 可配置项 -------------------- */
#define MM_LOG_PATH        "/mm.log"
#define MM_LOG_BUF_SIZE    (8 * 1024)
#define MM_LOG_LINE_MAX    192

#define SYS_LOG_PATH       "/sys.log"
#define SYS_LOG_LINE_MAX   192

/* SYS 日志在 SYSTEM 任务里只能进内存缓冲，不能直接落盘 */
#define SYS_RING_SIZE      (8 * 1024)   /* 环形缓冲大小 */

/* -------------------- 简易自旋锁（只保护内存结构，不要包住文件IO） -------------------- */
static inline int xchg(volatile int* p, int v)
{
    int old;
    __asm__ __volatile__("xchg %0, %1"
                         : "=r"(old), "+m"(*p)
                         : "0"(v)
                         : "memory");
    return old;
}

static volatile int g_log_lock = 0;

static void log_lock(void)
{
    while (xchg(&g_log_lock, 1) == 1) { /* spin */ }
}

static void log_unlock(void)
{
    xchg(&g_log_lock, 0);
}

/* -------------------- RTC 时间 -------------------- */
static int g_mm_log_enabled  = 1;
static int g_sys_log_enabled = 1;

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

/* =========================================================
 *                      MM LOG
 * ========================================================= */
static int  g_mm_log_fd = -1;
static char g_mm_log_buf[MM_LOG_BUF_SIZE];
static int  g_mm_log_buf_len = 0;

static void mm_log_try_open(void)
{
    if (g_mm_log_fd >= 0)
        return;

    int flags = O_CREAT | O_RDWR;

    /*
     * 若你的 open 是 3 参数：open(path, flags, mode)，就改成：
     * g_mm_log_fd = open(MM_LOG_PATH, flags, 0644);
     */
    g_mm_log_fd = open(MM_LOG_PATH, flags);
    if (g_mm_log_fd < 0)
        return;

    lseek(g_mm_log_fd, 0, SEEK_END);

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
        lseek(g_mm_log_fd, 0, SEEK_END);
        write(g_mm_log_fd, (void*)s, len);
        return;
    }

    if (g_mm_log_buf_len + len > MM_LOG_BUF_SIZE) {
        return; /* 缓冲满：丢弃 */
    }

    memcpy(g_mm_log_buf + g_mm_log_buf_len, (void*)s, len);
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

/* =========================================================
 *                      SYS LOG (内存环形缓冲 + 非SYS落盘)
 * ========================================================= */

/* /sys.log 只能由非 TASK_SYS 来 open/write */
static int g_sys_log_fd = -1;

/* 环形缓冲 */
static char g_sys_ring[SYS_RING_SIZE];
static int  g_sys_head = 0; /* 写指针 */
static int  g_sys_tail = 0; /* 读指针 */

static int sys_ring_used_nolock(void)
{
    if (g_sys_head >= g_sys_tail)
        return g_sys_head - g_sys_tail;
    return SYS_RING_SIZE - (g_sys_tail - g_sys_head);
}

static int sys_ring_free_nolock(void)
{
    /* 留 1 字节区分空/满 */
    return SYS_RING_SIZE - sys_ring_used_nolock() - 1;
}

static void sys_ring_drop_oldest_nolock(int bytes)
{
    while (bytes-- > 0) {
        g_sys_tail++;
        if (g_sys_tail >= SYS_RING_SIZE)
            g_sys_tail = 0;
    }
}

static void sys_ring_push(const char* s, int len)
{
    if (!s || len <= 0)
        return;

    log_lock();

    /* 如果单条日志比 ring 还大：只保留末尾 */
    if (len >= SYS_RING_SIZE) {
        s   = s + (len - (SYS_RING_SIZE - 1));
        len = SYS_RING_SIZE - 1;
        g_sys_head = 0;
        g_sys_tail = 0;
    }

    int free = sys_ring_free_nolock();
    if (free < len) {
        /* 不够就丢最老的，保证能塞进来 */
        sys_ring_drop_oldest_nolock(len - free);
    }

    int first = SYS_RING_SIZE - g_sys_head;
    if (first > len) first = len;

    memcpy(g_sys_ring + g_sys_head, (void*)s, first);
    g_sys_head += first;
    if (g_sys_head >= SYS_RING_SIZE) g_sys_head = 0;

    int left = len - first;
    if (left > 0) {
        memcpy(g_sys_ring + g_sys_head, (void*)(s + first), left);
        g_sys_head += left;
        if (g_sys_head >= SYS_RING_SIZE) g_sys_head = 0;
    }

    log_unlock();
}

/* 从 ring 取一段到 out，返回实际取到的字节数 */
static int sys_ring_pop(char* out, int maxlen)
{
    if (!out || maxlen <= 0)
        return 0;

    log_lock();

    int used = sys_ring_used_nolock();
    if (used <= 0) {
        log_unlock();
        return 0;
    }

    int n = used;
    if (n > maxlen) n = maxlen;

    int cont = (g_sys_head >= g_sys_tail) ? (g_sys_head - g_sys_tail)
                                          : (SYS_RING_SIZE - g_sys_tail);
    if (cont > n) cont = n;

    memcpy(out, g_sys_ring + g_sys_tail, cont);
    g_sys_tail += cont;
    if (g_sys_tail >= SYS_RING_SIZE) g_sys_tail = 0;

    int left = n - cont;
    if (left > 0) {
        memcpy(out + cont, g_sys_ring + g_sys_tail, left);
        g_sys_tail += left;
        if (g_sys_tail >= SYS_RING_SIZE) g_sys_tail = 0;
    }

    log_unlock();
    return n;
}

static int sys_ring_used(void)
{
    int used;
    log_lock();
    used = sys_ring_used_nolock();
    log_unlock();
    return used;
}

/* 只能在非 TASK_SYS 调用：打开 /sys.log */
static void sys_log_try_open_non_sys(void)
{
    if (g_sys_log_fd >= 0)
        return;

    int flags = O_CREAT | O_RDWR;

    /*
     * 若你的 open 是 3 参数：open(path, flags, mode)，就改成：
     * g_sys_log_fd = open(SYS_LOG_PATH, flags, 0644);
     */
    g_sys_log_fd = open(SYS_LOG_PATH, flags);
    if (g_sys_log_fd < 0)
        return;

    lseek(g_sys_log_fd, 0, SEEK_END);
}

/* 非 TASK_SYS：把 ring 里的内容落盘 */
void log_sys_flush(void)
{
    /* 先尝试打开文件；打开失败就不消耗 ring 里的数据 */
    sys_log_try_open_non_sys();
    if (g_sys_log_fd < 0)
        return;

    char tmp[512];
    int  n;

    while ((n = sys_ring_pop(tmp, sizeof(tmp))) > 0) {
        lseek(g_sys_log_fd, 0, SEEK_END);
        write(g_sys_log_fd, tmp, n);
    }
}

void log_sys_close(void)
{
    if (g_sys_log_fd >= 0) {
        close(g_sys_log_fd);
        g_sys_log_fd = -1;
    }
}

void log_sys_enable(int enable)
{
    g_sys_log_enabled = (enable != 0);
}

static const char* sys_type_name(int msgtype)
{
    switch (msgtype) {
    case GET_TICKS:      return "GET_TICKS";
    case GET_PID:        return "GET_PID";
    case GET_RTC_TIME:   return "GET_RTC_TIME";
    case GET_PROCS:      return "GET_PROCS";
    case CLEAR_SCREEN:   return "CLEAR_SCREEN";
    default:             return "UNKNOWN";
    }
}

/*
 * 关键修改点：
 *  - 这里绝对不能 open/write 文件（SYSTEM 任务会自死锁）
 *  - 只 printl + push 到内存 ring
 */
void log_sys_event(int msgtype, int src, int val)
{
    if (!g_sys_log_enabled)
        return;

    struct time t;
    get_rtc_time_log(&t);

    char line[SYS_LOG_LINE_MAX];
    int n;

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] SYS_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, sys_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] SYS_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, sys_type_name(msgtype), val);
    }

    printl("%s", line);
    sys_ring_push(line, n);
}

/* 为了不改其它任务：在 MM 打日志时顺手把 SYS ring 落盘 */
static void log_sys_flush_poll(void)
{
    static int cnt = 0;
    int used = sys_ring_used();

    /* ring 超过一半，或每 16 次 MM 日志触发一次 flush */
    cnt++;
    if (used < (SYS_RING_SIZE / 2) && (cnt < 16))
        return;

    cnt = 0;
    log_sys_flush(); /* 非 TASK_SYS：安全 */
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

    /* 顺手刷一下 SYS 日志（避免 SYSTEM 任务里做文件IO） */
    log_sys_flush_poll();
}