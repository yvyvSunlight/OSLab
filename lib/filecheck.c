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
 *                                set_checksum
 *****************************************************************************/
/**
 * 设置文件的MD5校验和
 * 
 * @param pathname   文件路径
 * @param md5_str    MD5十六进制字符串（32字符）
 * @param key        用于计算MD5的key
 * @return           0 成功，-1 失败
 */
PUBLIC int set_checksum(const char *pathname, const char *md5_str, u32 key)
{
    MESSAGE msg;

    msg.type     = SET_CHECKSUM;
    msg.PATHNAME = (void*)pathname;
    msg.NAME_LEN = strlen(pathname);
    msg.BUF      = (void*)md5_str;      /* MD5字符串 */
    msg.BUF_LEN  = 32;                  /* MD5长度 */
    msg.CHECKSUM_KEY = key;             /* 存储key */

    send_recv(BOTH, TASK_FS, &msg);
    assert(msg.type == SYSCALL_RET);

    return msg.RETVAL;
}

/*****************************************************************************
 *                                get_checksum
 *****************************************************************************/
/**
 * 获取文件的MD5校验和及key
 * 
 * @param pathname   文件路径
 * @param md5_buf    输出缓冲区（至少33字节）
 * @param key_out    输出key指针
 * @return           0 成功，-1 失败
 */
PUBLIC int get_checksum(const char *pathname, char *md5_buf, u32 *key_out)
{
    MESSAGE msg;

    msg.type     = GET_CHECKSUM;
    msg.PATHNAME = (void*)pathname;
    msg.NAME_LEN = strlen(pathname);
    msg.BUF      = (void*)md5_buf;      /* 输出缓冲区 */
    msg.BUF_LEN  = 33;                  /* 缓冲区大小 */

    send_recv(BOTH, TASK_FS, &msg);
    assert(msg.type == SYSCALL_RET);

    if (msg.RETVAL == 0 && key_out != 0) {
        *key_out = msg.CHECKSUM_KEY;
    }

    return msg.RETVAL;
}
