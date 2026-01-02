c msgtype)
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
    case CALC_CHECKSUM: return "CALC_CHECKSUM";
    case VERIFY_CHECKSUM: return "VERIFY_CHECKSUM";
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

/* IO helpers */
// 打开文件，若超过 max_bytes 则删除重建
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

// 正常写入
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

// 写入并可能重置文件
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
            rotated = 0;
            continue;
        }

        if (rotated) break;

        close(*pfd);
        unlink(path);
        *pfd = open(path, O_CREAT | O_RDWR);
        if (*pfd < 0) break;

        rotated = 1;
    }

    (void)max_file;
}

/* flush common */
// 将数据写入文件
static void flush_common(ringbuf_t* rb, const char* path, int max_file, int suppress_fs, int suppress_hd, volatile int* self_flag_to_set)
{
    char tmp[4096];
    int n;

    n = ring_pop(rb, tmp, sizeof(tmp));
    if (n <= 0) return;

    suppress_begin(suppress_fs, suppress_hd, self_flag_to_set);

    {
        int fd = open_append_rotate(path, max_file);
        if (fd >= 0) {
            int wrote = write_all_once(fd, tmp, n);
            if (wrote < n) {
                write_all_may_rotate(&fd, path, max_file, tmp + wrote, n - wrote);
            }

            while ((n = ring_pop(rb, tmp, sizeof(tmp))) > 0) {
                wrote = write_all_once(fd, tmp, n);
                if (wrote < n) {
                    write_all_may_rotate(&fd, path, max_file, tmp + wrote, n - wrote);
                }
            }
            close(fd);
        } else {
            log_lock();
            rb->kick_armed = 0;
            log_unlock();
        }
    }

    suppress_end(suppress_fs, suppress_hd, self_flag_to_set);
}

/* event APIs: 只写缓冲区 */
// 采集日志信息
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

// flush APIs
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
c