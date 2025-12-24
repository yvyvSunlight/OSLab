/*************************************************************************//**
 *****************************************************************************
 * @file   checksum.c
 * @brief  checksum helpers
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

PUBLIC int set_checksum(const char *pathname, int checksum)
{
	MESSAGE msg;

	msg.type     = SET_CHECKSUM;
	msg.PATHNAME = (void*)pathname;
	msg.NAME_LEN = strlen(pathname);
	msg.CHECKSUM = checksum;

	send_recv(BOTH, TASK_FS, &msg);
	assert(msg.type == SYSCALL_RET);

	return msg.RETVAL;
}

PUBLIC int get_checksum(const char *pathname)
{
	MESSAGE msg;

	msg.type     = GET_CHECKSUM;
	msg.PATHNAME = (void*)pathname;
	msg.NAME_LEN = strlen(pathname);

	send_recv(BOTH, TASK_FS, &msg);
	assert(msg.type == SYSCALL_RET);

	return msg.RETVAL;
}
