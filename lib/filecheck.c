/*************************************************************************//**
 *****************************************************************************
 * @file   lib/filecheck.c
 * @brief  文件校验 - 用户态消息发送接口
 * @date   2026
 *****************************************************************************
 *****************************************************************************/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

/*****************************************************************************
 *                                get_checksum
 *****************************************************************************/
/**
 * 获取文件的MD5校验和
 * 
 * @param pathname   文件路径
 * @param md5_buf    输出缓冲区（至少33字节）
 * @return           0 成功，-1 失败
 */
PUBLIC int get_checksum(const char *pathname, char *md5_buf)
{
    MESSAGE msg;

    msg.type     = GET_CHECKSUM;
    msg.PATHNAME = (void*)pathname;
    msg.NAME_LEN = strlen(pathname);
    msg.BUF      = (void*)md5_buf;      /* 输出缓冲区 */
    msg.BUF_LEN  = 33;                  /* 缓冲区大小 */

    send_recv(BOTH, TASK_FS, &msg);
    assert(msg.type == SYSCALL_RET);

    return msg.RETVAL;
}

/*****************************************************************************
 *                                calc_checksum
 *****************************************************************************/
PUBLIC int calc_checksum(const char *pathname, char *md5_buf)
{
    MESSAGE msg;

    msg.type     = CALC_CHECKSUM;
    msg.PATHNAME = (void*)pathname;
    msg.NAME_LEN = strlen(pathname);
    msg.BUF      = (void*)md5_buf;
    msg.BUF_LEN  = 33;

    send_recv(BOTH, TASK_FS, &msg);
    assert(msg.type == SYSCALL_RET);
    return msg.RETVAL;
}

/*****************************************************************************
 *                                verify_checksum
 *****************************************************************************/
PUBLIC int verify_checksum(const char *pathname)
{
    MESSAGE msg;

    msg.type     = VERIFY_CHECKSUM;
    msg.PATHNAME = (void*)pathname;
    msg.NAME_LEN = strlen(pathname);

    send_recv(BOTH, TASK_FS, &msg);
    assert(msg.type == SYSCALL_RET);
    return msg.RETVAL;
}

/*****************************************************************************
 *                                refresh_checksums
 *****************************************************************************/
/**
 * 刷新所有可执行文件的校验和（仅 INIT 进程可调用）
 * FS 会遍历根目录下所有普通文件，用当前 key 重新计算并写入 inode
 * 
 * @return  0 成功，-1 失败
 */
PUBLIC int refresh_checksums()
{
    MESSAGE msg;

    msg.type = REFRESH_CHECKSUMS;

    send_recv(BOTH, TASK_FS, &msg);
    assert(msg.type == SYSCALL_RET);
    return msg.RETVAL;
}
