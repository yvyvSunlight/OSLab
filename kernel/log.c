/*
@Author  : Ramoor
@Date    : 2026-01-01
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

/* ---------------- paths ---------------- */
#define MM_LOG_PATH     "/mm.log"
#define SYS_LOG_PATH    "/sys.log"
#define FS_LOG_PATH     "/fs.log"
#define HD_LOG_PATH     "/hd.log"

/* ---------------- ring sizes ---------------- */
#define MM_RING_SIZE    (8 * 1024)
#define SYS_RING_SIZE   (8 * 1024)
#define FS_RING_SIZE    (8 * 1024)
#define HD_RING_SIZE    (8 * 1024)

/* ---------------- line max ---------------- */
#define MM_LOG_LINE_MAX  192
#define SYS_LOG_LINE_MAX 192
#define FS_LOG_LINE_MAX  192
#define HD_LOG_LINE_MAX  192

/* ---------------- file max (rotate) ---------------- */
#define MM_LOG_MAX_FILE   (64 * 1024)
#define SYS_LOG_MAX_FILE  (64 * 1024)
#define FS_LOG_MAX_FILE   (64 * 1024)
#define HD_LOG_MAX_FILE   (64 * 1024)

/* 是否把日志同时printl到控制台 */
#define LOG_ECHO_CONSOLE  0

/* 日志开关 */
//#define LOG_ENABLE_TASK 1
#define MM_LOG_ENABLE  1
#define SYS_LOG_ENABLE 1
#define FS_LOG_ENABLE  1
#define HD_LOG_ENABLE  1

/* =========================================================
 *  嵌套锁实现
 * ========================================================= */
#define EFLAGS_IF 0x200 // 定义中断允许标志位

// 安全读取EFLAGS寄存器
static inline u32 read_eflags(void)
{
    u32 f;
    __asm__ __volatile__("pushfl; popl %0" : "=r"(f));
    return f;
}

static int g_log_nest = 0;       // 嵌套层
static int g_log_prev_if = 0;    // 上一层是否允许中断

// 加锁（支持嵌套）
static void log_lock(void)
{
    if (g_log_nest == 0) {
        g_log_prev_if = (read_eflags() & EFLAGS_IF) ? 1 : 0;
        disable_int();
    }
    g_log_nest++;
}

// 解锁（支持嵌套）
static void log_unlock(void)
{
    g_log_nest--;
    if (g_log_nest == 0 && g_log_prev_if) {
        enable_int();
    }
}

/* =========================================================
 * enable / suppress
 * ========================================================= */

static volatile int g_fs_suppress = 0;
static volatile int g_hd_suppress = 0;


// 抑制日志，防止进入循环
static void suppress_begin(int set_fs, int set_hd, volatile int* self_flag)
{
    log_lock();
    if (self_flag) *self_flag = 1;
    if (set_fs) g_fs_suppress = 1;
    if (set_hd) g_hd_suppress = 1;
    log_unlock();
}

// 结束抑制日志
static void suppress_end(int set_fs, int set_hd, volatile int* self_flag)
{
    log_lock();
    if (self_flag) *self_flag = 0;
    if (set_fs) g_fs_suppress = 0;
    if (set_hd) g_hd_suppress = 0;
    log_unlock();
}

/* =========================================================
 * RTC
 * ========================================================= */
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
 * ring buffer
 * ========================================================= */
typedef struct ringbuf {
    char* buf;
    int   size;
    int   head;
    int   tail;
} ringbuf_t;

// 计算已用空间
static int ring_used_nolock(const ringbuf_t* r)
{
    return (r->head >= r->tail) ? (r->head - r->tail) : (r->size - (r->tail - r->head));
}

// 计算空闲空间
static int ring_free_nolock(const ringbuf_t* r)
{
    return r->size - ring_used_nolock(r) - 1;
}

// 丢弃最旧的数据
static void ring_drop_oldest_nolock(ringbuf_t* r, int bytes)
{
    while (bytes-- > 0) {
        r->tail++;
        if (r->tail >= r->size) r->tail = 0;
    }
}

// 推入数据
static void ring_push(ringbuf_t* r, const char* s, int len)
{
    if (!r || !s || len <= 0) return;

    log_lock();

    if (len >= r->size) {
        s   += (len - (r->size - 1));
        len  = r->size - 1;
        r->head = r->tail = 0;
    }

    {
        int free = ring_free_nolock(r);
        if (free < len) ring_drop_oldest_nolock(r, len - free);
    }

    {
        int first = r->size - r->head;
        if (first > len) first = len;

        memcpy(r->buf + r->head, (void*)s, first);
        r->head += first;
        if (r->head >= r->size) r->head = 0;

        {
            int left = len - first;
            if (left > 0) {
                memcpy(r->buf + r->head, (void*)(s + first), left);
                r->head += left;
                if (r->head >= r->size) r->head = 0;
            }
        }
    }

    log_unlock();
}

// 弹出数据
static int ring_pop(ringbuf_t* r, char* out, int maxlen)
{
    int used, n, cont, left;

    if (!r || !out || maxlen <= 0) return 0;

    log_lock();

    used = ring_used_nolock(r);
    if (used <= 0) { log_unlock(); return 0; }

    n = used;
    if (n > maxlen) n = maxlen;

    cont = (r->head >= r->tail) ? (r->head - r->tail) : (r->size - r->tail);
    if (cont > n) cont = n;

    memcpy(out, r->buf + r->tail, cont);
    r->tail += cont;
    if (r->tail >= r->size) r->tail = 0;

    left = n - cont;
    if (left > 0) {
        memcpy(out + cont, r->buf + r->tail, left);
        r->tail += left;
        if (r->tail >= r->size) r->tail = 0;
    }

    log_unlock();
    return n;
}

/* 实例化四个 ring */
static char g_mm_ring[MM_RING_SIZE];
static char g_sys_ring[SYS_RING_SIZE];
static char g_fs_ring[FS_RING_SIZE];
static char g_hd_ring[HD_RING_SIZE];

static ringbuf_t g_mm_rb  = { g_mm_ring,  MM_RING_SIZE,  0, 0 };
static ringbuf_t g_sys_rb = { g_sys_ring, SYS_RING_SIZE, 0, 0 };
static ringbuf_t g_fs_rb  = { g_fs_ring,  FS_RING_SIZE,  0, 0 };
static ringbuf_t g_hd_rb  = { g_hd_ring,  HD_RING_SIZE,  0, 0 };

/* =========================================================
 * name
 * ========================================================= */
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

/* =========================================================
 * IO helpers
 * ========================================================= */
// 打开文件，若超过 max_bytes 则 rotate（删除重建）
 static int open_append_rotate(const char* path, int max_bytes)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) fd = open(path, O_CREAT | O_RDWR);
    if (fd < 0) return -1;

    {
        int sz = lseek(fd, 0, SEEK_END);
        if (sz < 0) sz = 0;

        if (sz >= max_bytes) {
            close(fd);
            unlink(path);
            fd = open(path, O_CREAT | O_RDWR);
            if (fd < 0) return -1;
        }
    }
    return fd;
}

// 文件写入工具：尽量写完，返回写入的字节数
static int write_all_once(int fd, const char* buf, int n)
{
    int off = 0;
    while (off < n) {
        int w = write(fd, buf + off, n - off);
        if (w <= 0) break;
        off += w;
    }
    return off;
}

// 特殊情况下的写入：写失败时尝试rotate
static void write_all_may_rotate(int* pfd, const char* path, int max_file,
                                const char* buf, int n)
{
    int off = 0;
    int rotated = 0;

    if (!pfd || *pfd < 0 || !buf || n <= 0) return;

    while (off < n) {
        int w = write(*pfd, buf + off, n - off);
        if (w > 0) {
            off += w;
            rotated = 0; /* 有进展，允许下次再rotate */
            continue;
        }

        /* write失败：尝试rotate一次 */
        if (rotated) break; /* rotate也没用，避免死循环 */

        close(*pfd);
        unlink(path);
        *pfd = open(path, O_CREAT | O_RDWR);
        if (*pfd < 0) break;

        /* rotate后也可能需要再定位到末尾（新文件默认在0，无需lseek） */
        rotated = 1;
    }

    (void)max_file; /* 保留参数，不改变原功能接口/语义（rotate阈值仍在open阶段生效） */
}

/* =========================================================
 * flush common（统一模板）
 * ========================================================= */
// 通用flush函数模板
static void flush_common(ringbuf_t* rb, const char* path, int max_file, int suppress_fs, int suppress_hd, volatile int* self_flag_to_set)
{
    char tmp[512];
    int n;

    n = ring_pop(rb, tmp, sizeof(tmp));
    if (n <= 0) return;

    suppress_begin(suppress_fs, suppress_hd, self_flag_to_set);

    {
        int fd = open_append_rotate(path, max_file);
        if (fd >= 0) {
            /* 先正常写；若写失败则异常路径rotate并继续 */
            int wrote = write_all_once(fd, tmp, n);
            if (wrote < n) {
                /* 让“满了不刷新”的情况能恢复写入 */
                write_all_may_rotate(&fd, path, max_file, tmp + wrote, n - wrote);
            }

            while ((n = ring_pop(rb, tmp, sizeof(tmp))) > 0) {
                wrote = write_all_once(fd, tmp, n);
                if (wrote < n) {
                    write_all_may_rotate(&fd, path, max_file, tmp + wrote, n - wrote);
                    /* rotate后仍可能失败，继续下一块避免卡死 */
                }
            }
            close(fd);
        }
    }

    suppress_end(suppress_fs, suppress_hd, self_flag_to_set);
}

/* =========================================================
 * event APIs: 只写ring（不落盘）
 * ========================================================= */
void log_sys_event(int msgtype, int src, int val)
{
    struct time t;
    char line[SYS_LOG_LINE_MAX];
    int n;

    if (!SYS_LOG_ENABLE) return;

    get_rtc_time_log(&t);

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d] SYS_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, sys_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d] SYS_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, sys_type_name(msgtype), val);
    }

#if LOG_ECHO_CONSOLE
    printl("%s", line);
#endif
    ring_push(&g_sys_rb, line, n);
}

void log_mm_event(int msgtype, int src, int val)
{
    struct time t;
    char line[MM_LOG_LINE_MAX];
    int n;

    if (!MM_LOG_ENABLE) return;

    get_rtc_time_log(&t);

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d] MM_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, mm_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d] MM_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, mm_type_name(msgtype), val);
    }

#if LOG_ECHO_CONSOLE
    printl("%s", line);
#endif
    ring_push(&g_mm_rb, line, n);
}

void log_fs_event(int msgtype, int src, int val)
{
    struct time t;
    char line[FS_LOG_LINE_MAX];
    int n;

    if (!FS_LOG_ENABLE) return;
    if (g_fs_suppress) return;

    get_rtc_time_log(&t);

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d] FS_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, fs_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d] FS_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, fs_type_name(msgtype), val);
    }

#if LOG_ECHO_CONSOLE
    printl("%s", line);
#endif
    ring_push(&g_fs_rb, line, n);
}

void log_hd_event(int msgtype, int src, int dev, int val)
{
    struct time t;
    char line[HD_LOG_LINE_MAX];
    int n;

    if (!HD_LOG_ENABLE) return;
    if (g_hd_suppress) return;

    dev = (dev > 256) ? (dev & 0xFF) : (dev & 0xF);

    get_rtc_time_log(&t);

    if (val == -1) {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d dev=%d] HD_%s\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, dev, hd_type_name(msgtype));
    } else {
        n = sprintf(line,
            "<%d-%02d-%02d %02d:%02d:%02d>\n [pid=%d dev=%d] HD_%s val=%d\n",
            t.year, t.month, t.day, t.hour, t.minute, t.second,
            src, dev, hd_type_name(msgtype), val);
    }

#if LOG_ECHO_CONSOLE
    printl("%s", line);
#endif
    ring_push(&g_hd_rb, line, n);
}

/* =========================================================
 * flush APIs: 接口保持不变
 * ========================================================= */
void log_mm_flush(void)
{
    flush_common(&g_mm_rb, MM_LOG_PATH, MM_LOG_MAX_FILE, 1, 1, 0);
}

void log_sys_flush(void)
{
    flush_common(&g_sys_rb, SYS_LOG_PATH, SYS_LOG_MAX_FILE, 1, 1, 0);
}

void log_fs_flush(void)
{
    flush_common(&g_fs_rb, FS_LOG_PATH, FS_LOG_MAX_FILE, 0, 0, &g_fs_suppress);
}

void log_hd_flush(void)
{
    flush_common(&g_hd_rb, HD_LOG_PATH, HD_LOG_MAX_FILE, 0, 0, &g_hd_suppress);
}