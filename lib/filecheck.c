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

// PUBLIC int calc_checksum(const char *pathname, char *md5_buf)
// {
//     MESSAGE msg;

//     msg.type     = CALC_CHECKSUM;
//     msg.PATHNAME = (void*)pathname;
//     msg.NAME_LEN = strlen(pathname);
//     msg.BUF      = (void*)md5_buf;
//     msg.BUF_LEN  = 33;

//     send_recv(BOTH, TASK_FS, &msg);
//     assert(msg.type == SYSCALL_RET);
//     return msg.RETVAL;
// }

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

PUBLIC int refresh_checksums()
{
    MESSAGE msg;

    msg.type = REFRESH_CHECKSUMS;

    send_recv(BOTH, TASK_FS, &msg);
    assert(msg.type == SYSCALL_RET);
    return msg.RETVAL;
}
