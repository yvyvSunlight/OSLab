/*
@Author  : Ramoor
@Date    : 2025-12-29
*/
#ifndef _INCLUDE_LOG_H_
#define _INCLUDE_LOG_H_

/* seek whence */
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/* open flags：如果工程里本来就定义了，这里不会覆盖 */
#ifndef O_CREAT
#define O_CREAT  1
#endif

#ifndef O_RDWR
#define O_RDWR   0
#endif

/* mm */
void log_mm_enable(int enable);
void log_mm_event(int msgtype, int src, int val);
void log_mm_flush(void);
void log_mm_close(void);

/* sys */
void log_sys_enable(int enable);
void log_sys_event(int msgtype, int src, int val);

void log_sys_flush(void);
void log_sys_close(void);
#endif