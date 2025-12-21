/*************************************************************************/
/****************************************************************************
 * @file   kill.c
 * @brief  kill()
 * @author GitHub Copilot
 * @date   2025
 ****************************************************************************/
/****************************************************************************/

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

/****************************************************************************
 *                                kill
 ****************************************************************************
 * Terminate a process by pid.
 *
 * @param pid Target process id.
 *
 * @return 0 on success, -1 otherwise.
 ****************************************************************************/
PUBLIC int kill(int pid)
{
    MESSAGE msg;
    msg.type = KILL;
    msg.PID = pid;
    msg.STATUS = 9;

    send_recv(BOTH, TASK_MM, &msg);
    assert(msg.type == SYSCALL_RET);

    return msg.RETVAL;
}
