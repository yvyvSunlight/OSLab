/*************************************************************************/
/****************************************************************************
 * @file   clear.c
 * @brief  console_clear()
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

PUBLIC int clear_screen_cmd()
{
    MESSAGE msg;
    msg.type = CLEAR_SCREEN;

    send_recv(BOTH, TASK_SYS, &msg);
    assert(msg.type == SYSCALL_RET);

    return msg.RETVAL;
}
