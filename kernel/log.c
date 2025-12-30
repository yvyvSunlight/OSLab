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

#define MM_LOG_PATH     "/mm.log"
#define SYS_LOG_PATH    "/sys.log"
#define MM_RING_SIZE    (8 * 1024)
#define SYS_RING_SIZE   (8 * 1024)
#define MM_LOG_LINE_MAX 192
#define SYS_LOG_LINE_MAX 192

/* 单核：用关中断保护临界区，避免“拿锁后被抢占”导致永远自旋 */
static void log_lock(void)
{
    disable_int();
}
static void log_unlock(void)
{
    enable_int();
}

/* ---------- RTC ---------- */
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

/* ---------- ring helpers ---------- */
static int ring_used_nolock(int head, int tail, int size)
{
    return (head >= tail) ? (head - tail) : (size - (tail - head));
}
static int ring_free_nolock(int head, int tail, int size)
{
    return size - ring_used_nolock(head, tail, size) - 1; /* 留1字节区分空/满 */
}
static void ring_drop_oldest(int *tail, int bytes, int size)
{
    while (bytes-- > 0) {
        (*tail)++;
        if (*tail >= size) *tail = 0;
    }
}

/* ====================== MM ring ====================== */
static char g_mm_ring[MM_RING_SIZE];
static int  g_mm_head = 0;
static int  g_mm_tail = 0;

static void mm_ring_push(const char* s, int len)
{
    if (!s || len <= 0) return;

    log_lock();

    if (len >= MM_RING_SIZE) {
        s   += (len - (MM_RING_SIZE - 1));
        len  = MM_RING_SIZE - 1;
        g_mm_head = g_mm_tail = 0;
    }

    int free = ring_free_nolock(g_mm_head, g_mm_tail, MM_RING_SIZE);
    if (free < len) ring_drop_oldest(&g_mm_tail, len - free, MM_RING_SIZE);

    int first = MM_RING_SIZE - g_mm_head;
    if (first > len) first = len;
    memcpy(g_mm_ring + g_mm_head, (void*)s, first);
    g_mm_head += first;
    if (g_mm_head >= MM_RING_SIZE) g_mm_head = 0;

    int left = len - first;
    if (left > 0) {
        memcpy(g_mm_ring + g_mm_head, (void*)(s + first), left);
        g_mm_head += left;
        if (g_mm_head >= MM_RING_SIZE) g_mm_head = 0;
    }

    log_unlock();
}

static int mm_ring_pop(char* out, int maxlen)
{
    if (!out || maxlen <= 0) return 0;

    log_lock();

    int used = ring_used_nolock(g_mm_head, g_mm_tail, MM_RING_SIZE);
    if (used <= 0) { log_unlock(); return 0; }

    int n = used;
    if (n > maxlen) n = maxlen;

    int cont = (g_mm_head >= g_mm_tail) ? (g_mm_head - g_mm_tail)
                                        : (MM_RING_SIZE - g_mm_tail);
    if (cont > n) cont = n;

    memcpy(out, g_mm_ring + g_mm_tail, cont);
    g_mm_tail += cont;
    if (g_mm_tail >= MM_RING_SIZE) g_mm_tail = 0;

    int left = n - cont;
    if (left > 0) {
        memcpy(out + cont, g_mm_ring + g_mm_tail, left);
        g_mm_tail += left;
        if (g_mm_tail >= MM_RING_SIZE) g_mm_tail = 0;
    }

    log_unlock();
    return n;
}

/* ====================== SYS ring ====================== */
static char g_sys_ring[SYS_RING_SIZE];
static int  g_sys_head = 0;
static int  g_sys_tail = 0;

static void sys_ring_push(const char* s, int len)
{
    if (!s || len <= 0) return;

    log_lock();

    if (len >= SYS_RING_SIZE) {
        s   += (len - (SYS_RING_SIZE - 1));
        len  = SYS_RING_SIZE - 1;
        g_sys_head = g_sys_tail = 0;
    }

    int free = ring_free_nolock(g_sys_head, g_sys_tail, SYS_RING_SIZE);
    if (free < len) ring_drop_oldest(&g_sys_tail, len - free, SYS_RING_SIZE);

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

static int sys_ring_pop(char* out, int maxlen)
{
    if (!out || maxlen <= 0) return 0;

    log_lock();

    int used = ring_used_nolock(g_sys_head, g_sys_tail, SYS_RING_SIZE);
    if (used <= 0) { log_unlock(); return 0; }

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

/* ---------- name helpers ---------- */
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
static const char* sys_type_name(int msgtype)
{
    switch (msgtype) {
    case GET_TICKS:    return "GET_TICKS";
    case GET_PID:      return "GET_PID";
    case GET_RTC_TIME: return "GET_RTC_TIME";
    case GET_PROCS:    return "GET_PROCS";
    case CLEAR_SCREEN: return "CLEAR_SCREEN";
    default:           return "UNKNOWN";
    }
}

/* ---------- public controls ---------- */
void log_mm_enable(int enable)  { g_mm_log_enabled  = (enable != 0); }
void log_sys_enable(int enable) { g_sys_log_enabled = (enable != 0); }

/* ====================== event APIs: 只写ring，不落盘 ====================== */
void log_sys_event(int msgtype, int src, int val)
{
    if (!g_sys_log_enabled) return;

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

void log_mm_event(int msgtype, int src, int val)
{
    if (!g_mm_log_enabled) return;

    struct time t;
    get_rtc_time_log(&t);

    char line[MM_LOG_LINE_MAX];
    int n;

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] MM_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, mm_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] MM_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, mm_type_name(msgtype), val);
    }

    printl("%s", line);
    mm_ring_push(line, n);

    /* 关键：这里不允许再触发 open/write/flush */
}

/* ====================== flush APIs: 由 TASK_LOG 调用落盘 ====================== */
static int open_append_once(const char* path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) fd = open(path, O_CREAT | O_RDWR);
    if (fd < 0) return -1;
    lseek(fd, 0, SEEK_END);
    return fd;
}

void log_mm_flush(void)
{
    char tmp[512];

    /* 关键：先试着取一段；没有日志就直接返回，不要 open/write */
    int n = mm_ring_pop(tmp, sizeof(tmp));
    if (n <= 0) return;

    int fd = open_append_once(MM_LOG_PATH);
    if (fd < 0) {
        /* 打不开文件：把取出来的这段塞回去会比较麻烦；
           简化处理：丢弃这一小段（或你也可以改成“写回ring”） */
        return;
    }

    write(fd, tmp, n);
    while ((n = mm_ring_pop(tmp, sizeof(tmp))) > 0) {
        write(fd, tmp, n);
    }
    close(fd); /* 你们 FS 可能 close 才真正提交 */
}

void log_sys_flush(void)
{
    char tmp[512];

    int n = sys_ring_pop(tmp, sizeof(tmp));
    if (n <= 0) return;

    int fd = open_append_once(SYS_LOG_PATH);
    if (fd < 0) return;

    write(fd, tmp, n);
    while ((n = sys_ring_pop(tmp, sizeof(tmp))) > 0) {
        write(fd, tmp, n);
    }
    close(fd);
}


/* ======== append into log.c ======== */

/* ---------- FS/HD paths & sizes ---------- */
#define FS_LOG_PATH     "/fs.log"
#define HD_LOG_PATH     "/hd.log"
#define FS_RING_SIZE    (8 * 1024)
#define HD_RING_SIZE    (8 * 1024)
#define FS_LOG_LINE_MAX 192
#define HD_LOG_LINE_MAX 192

static int g_fs_log_enabled = 1;
static int g_hd_log_enabled = 1;

/* flush期间屏蔽：避免写日志导致自身日志无限增长 */
static int g_fs_suppress = 0;
static int g_hd_suppress = 0;

void log_fs_enable(int enable) { g_fs_log_enabled = (enable != 0); }
void log_hd_enable(int enable) { g_hd_log_enabled = (enable != 0); }

/* ====================== FS ring ====================== */
static char g_fs_ring[FS_RING_SIZE];
static int  g_fs_head = 0;
static int  g_fs_tail = 0;

static void fs_ring_push(const char* s, int len)
{
    if (!s || len <= 0) return;

    log_lock();

    if (len >= FS_RING_SIZE) {
        s   += (len - (FS_RING_SIZE - 1));
        len  = FS_RING_SIZE - 1;
        g_fs_head = g_fs_tail = 0;
    }

    int free = ring_free_nolock(g_fs_head, g_fs_tail, FS_RING_SIZE);
    if (free < len) ring_drop_oldest(&g_fs_tail, len - free, FS_RING_SIZE);

    int first = FS_RING_SIZE - g_fs_head;
    if (first > len) first = len;
    memcpy(g_fs_ring + g_fs_head, (void*)s, first);
    g_fs_head += first;
    if (g_fs_head >= FS_RING_SIZE) g_fs_head = 0;

    int left = len - first;
    if (left > 0) {
        memcpy(g_fs_ring + g_fs_head, (void*)(s + first), left);
        g_fs_head += left;
        if (g_fs_head >= FS_RING_SIZE) g_fs_head = 0;
    }

    log_unlock();
}

static int fs_ring_pop(char* out, int maxlen)
{
    if (!out || maxlen <= 0) return 0;

    log_lock();

    int used = ring_used_nolock(g_fs_head, g_fs_tail, FS_RING_SIZE);
    if (used <= 0) { log_unlock(); return 0; }

    int n = used;
    if (n > maxlen) n = maxlen;

    int cont = (g_fs_head >= g_fs_tail) ? (g_fs_head - g_fs_tail)
                                        : (FS_RING_SIZE - g_fs_tail);
    if (cont > n) cont = n;

    memcpy(out, g_fs_ring + g_fs_tail, cont);
    g_fs_tail += cont;
    if (g_fs_tail >= FS_RING_SIZE) g_fs_tail = 0;

    int left = n - cont;
    if (left > 0) {
        memcpy(out + cont, g_fs_ring + g_fs_tail, left);
        g_fs_tail += left;
        if (g_fs_tail >= FS_RING_SIZE) g_fs_tail = 0;
    }

    log_unlock();
    return n;
}

/* ====================== HD ring ====================== */
static char g_hd_ring[HD_RING_SIZE];
static int  g_hd_head = 0;
static int  g_hd_tail = 0;

static void hd_ring_push(const char* s, int len)
{
    if (!s || len <= 0) return;

    log_lock();

    if (len >= HD_RING_SIZE) {
        s   += (len - (HD_RING_SIZE - 1));
        len  = HD_RING_SIZE - 1;
        g_hd_head = g_hd_tail = 0;
    }

    int free = ring_free_nolock(g_hd_head, g_hd_tail, HD_RING_SIZE);
    if (free < len) ring_drop_oldest(&g_hd_tail, len - free, HD_RING_SIZE);

    int first = HD_RING_SIZE - g_hd_head;
    if (first > len) first = len;
    memcpy(g_hd_ring + g_hd_head, (void*)s, first);
    g_hd_head += first;
    if (g_hd_head >= HD_RING_SIZE) g_hd_head = 0;

    int left = len - first;
    if (left > 0) {
        memcpy(g_hd_ring + g_hd_head, (void*)(s + first), left);
        g_hd_head += left;
        if (g_hd_head >= HD_RING_SIZE) g_hd_head = 0;
    }

    log_unlock();
}

static int hd_ring_pop(char* out, int maxlen)
{
    if (!out || maxlen <= 0) return 0;

    log_lock();

    int used = ring_used_nolock(g_hd_head, g_hd_tail, HD_RING_SIZE);
    if (used <= 0) { log_unlock(); return 0; }

    int n = used;
    if (n > maxlen) n = maxlen;

    int cont = (g_hd_head >= g_hd_tail) ? (g_hd_head - g_hd_tail)
                                        : (HD_RING_SIZE - g_hd_tail);
    if (cont > n) cont = n;

    memcpy(out, g_hd_ring + g_hd_tail, cont);
    g_hd_tail += cont;
    if (g_hd_tail >= HD_RING_SIZE) g_hd_tail = 0;

    int left = n - cont;
    if (left > 0) {
        memcpy(out + cont, g_hd_ring + g_hd_tail, left);
        g_hd_tail += left;
        if (g_hd_tail >= HD_RING_SIZE) g_hd_tail = 0;
    }

    log_unlock();
    return n;
}

/* ---------- name helpers ---------- */
static const char* fs_type_name(int msgtype)
{
    switch (msgtype) {
    case OPEN:   return "OPEN";
    case CLOSE:  return "CLOSE";
    case READ:   return "READ";
    case WRITE:  return "WRITE";
    case LSEEK:  return "LSEEK";
    case UNLINK: return "UNLINK";
    case FORK:   return "FORK";
    case EXIT:   return "EXIT";
    case STAT:   return "STAT";
    case TRUNCATE: return "TRUNCATE";
    case SET_CHECKSUM: return "SET_CHECKSUM";
    case GET_CHECKSUM: return "GET_CHECKSUM";
    default:     return "UNKNOWN";
    }
}

static const char* hd_type_name(int msgtype)
{
    switch (msgtype) {
    case DEV_OPEN:  return "OPEN";
    case DEV_CLOSE: return "CLOSE";
    case DEV_READ:  return "READ";
    case DEV_WRITE: return "WRITE";
    case DEV_IOCTL: return "IOCTL";
    default:        return "UNKNOWN";
    }
}

/* ====================== event APIs: FS/HD 只写ring ====================== */
void log_fs_event(int msgtype, int src, int val)
{
    if (!g_fs_log_enabled) return;

    /* flush期间屏蔽，避免自喂 */
    if (g_fs_suppress) return;

    struct time t;
    get_rtc_time_log(&t);

    char line[FS_LOG_LINE_MAX];
    int n;

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] FS_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, fs_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d] FS_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, fs_type_name(msgtype), val);
    }

    printl("%s", line);
    fs_ring_push(line, n);
}

void log_hd_event(int msgtype, int src, int dev, int val)
{
    if (!g_hd_log_enabled) return;

    if (g_hd_suppress) return;

    struct time t;
    get_rtc_time_log(&t);

    char line[HD_LOG_LINE_MAX];
    int n;

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d dev=%d] HD_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, dev, hd_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d> [src=%d dev=%d] HD_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, dev, hd_type_name(msgtype), val);
    }

    printl("%s", line);
    hd_ring_push(line, n);
}

/* ====================== flush APIs: 由 TASK_LOG 落盘 ====================== */
void log_fs_flush(void)
{
    char tmp[512];

    int n = fs_ring_pop(tmp, sizeof(tmp));
    if (n <= 0) return;

    /* 关键：屏蔽 flush 自身引发的 FS 日志 */
    log_lock(); g_fs_suppress = 1; log_unlock();

    int fd = open_append_once(FS_LOG_PATH);
    if (fd < 0) {
        log_lock(); g_fs_suppress = 0; log_unlock();
        return;
    }

    write(fd, tmp, n);
    while ((n = fs_ring_pop(tmp, sizeof(tmp))) > 0) {
        write(fd, tmp, n);
    }
    close(fd);

    log_lock(); g_fs_suppress = 0; log_unlock();
}

void log_hd_flush(void)
{
    char tmp[512];

    int n = hd_ring_pop(tmp, sizeof(tmp));
    if (n <= 0) return;

    /* 关键：屏蔽 flush 自身引发的 HD 日志（写hd.log必然触发磁盘IO） */
    log_lock(); g_hd_suppress = 1; log_unlock();

    int fd = open_append_once(HD_LOG_PATH);
    if (fd < 0) {
        log_lock(); g_hd_suppress = 0; log_unlock();
        return;
    }

    write(fd, tmp, n);
    while ((n = hd_ring_pop(tmp, sizeof(tmp))) > 0) {
        write(fd, tmp, n);
    }
    close(fd);

    log_lock(); g_hd_suppress = 0; log_unlock();
}