/*************************************************************************/
/****************************************************************************
 * @file   getprocs.c
 * @brief  get_procs()
 ****************************************************************************/
/******************************************************************************/

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

PUBLIC int get_procs(struct proc_info *buf, int max)
{
    MESSAGE msg;

    msg.type = GET_PROCS;
    msg.BUF = buf;
    msg.CNT = max;
    msg.BUF_LEN = sizeof(struct proc_info);

    send_recv(BOTH, TASK_SYS, &msg);
    assert(msg.type == SYSCALL_RET);

    return msg.RETVAL;
}
