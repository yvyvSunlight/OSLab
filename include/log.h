/*
@Author  : Ramoor
@Date    : 2025-12-29
*/

#ifndef _INCLUDE_LOG_H_
#define _INCLUDE_LOG_H_

void log_mm_enable(int enable);
void log_sys_enable(int enable);

/* 在 MM/SYS 里调用也安全：只写内存 ring，不做文件I/O */
void log_mm_event(int msgtype, int src, int val);
void log_sys_event(int msgtype, int src, int val);

/* 只能在“非 MM/非 SYS”的上下文调用：由 TASK_LOG 调用落盘 */
void log_mm_flush(void);
void log_sys_flush(void);

void log_mm_close(void);
void log_sys_close(void);

#endif