/*************************************************************************//**
 *****************************************************************************
 * @file   systask.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   2007
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
#include "keyboard.h"
#include "proto.h"

PRIVATE int read_register(char reg_addr);
PRIVATE u32 get_rtc_time(struct time *t);

/*****************************************************************************
 *                                task_sys
 *****************************************************************************/
/**
 * <Ring 1> The main loop of TASK SYS.
 * 
 *****************************************************************************/
PUBLIC void task_sys()
{
	MESSAGE msg;
	struct time t;

	while (1) {
		send_recv(RECEIVE, ANY, &msg);
		int src = msg.source;

		switch (msg.type) {
		case GET_TICKS:
			msg.RETVAL = ticks;
			send_recv(SEND, src, &msg);
			break;
		case GET_PID:
			msg.type = SYSCALL_RET;
			msg.PID = src;
			send_recv(SEND, src, &msg);
			break;
		case GET_RTC_TIME:
			msg.type = SYSCALL_RET;
			get_rtc_time(&t);
			phys_copy(va2la(src, msg.BUF),
				  va2la(TASK_SYS, &t),
				  sizeof(t));
			send_recv(SEND, src, &msg);
			break;
		case GET_PROCS:
		{
			int limit = msg.CNT;
			int count = 0;
			if (limit > 0 && msg.BUF) {
				struct proc *p;
				for (p = &FIRST_PROC; p <= &LAST_PROC && count < limit; p++) {
					if (p->p_flags == FREE_SLOT)
						continue;
					struct proc_info info;
					memset(&info, 0, sizeof(info));
					info.pid = proc2pid(p);
					info.parent_pid = p->p_parent;
					info.flags = p->p_flags;
					info.queue_level = p->queue_level;
					info.ticks = p->ticks;
					info.priority = p->priority;
					{
						int copy_len = strlen(p->name);
						if (copy_len >= PROC_NAME_LEN)
							copy_len = PROC_NAME_LEN - 1;
						memcpy(info.name, p->name, copy_len);
						info.name[copy_len] = 0;
					}
					char *dest = (char*)msg.BUF + count * sizeof(struct proc_info);
					phys_copy(va2la(src, dest),
						  va2la(TASK_SYS, &info),
						  sizeof(info));
					count++;
				}
			}
			msg.type = SYSCALL_RET;
			msg.RETVAL = count;
			send_recv(SEND, src, &msg);
			break;
		}
		case CLEAR_SCREEN:
			console_clear(&console_table[current_console]);
			msg.type = SYSCALL_RET;
			msg.RETVAL = 0;
			send_recv(SEND, src, &msg);
			break;
		default:
			panic("unknown msg type");
			break;
		}
	}
}


/*****************************************************************************
 *                                get_rtc_time
 *****************************************************************************/
/**
 * Get RTC time from the CMOS
 * 
 * @return Zero.
 *****************************************************************************/
PRIVATE u32 get_rtc_time(struct time *t)
{
	t->year = read_register(YEAR);
	t->month = read_register(MONTH);
	t->day = read_register(DAY);
	t->hour = read_register(HOUR);
	t->minute = read_register(MINUTE);
	t->second = read_register(SECOND);

	if ((read_register(CLK_STATUS) & 0x04) == 0) {
		/* Convert BCD to binary (default RTC mode) */
		t->year = BCD_TO_DEC(t->year);
		t->month = BCD_TO_DEC(t->month);
		t->day = BCD_TO_DEC(t->day);
		t->hour = BCD_TO_DEC(t->hour);
		t->minute = BCD_TO_DEC(t->minute);
		t->second = BCD_TO_DEC(t->second);
	}

	t->year += 2000;

	return 0;
}

/*****************************************************************************
 *                                read_register
 *****************************************************************************/
/**
 * Read register from CMOS.
 * 
 * @param reg_addr 
 * 
 * @return 
 *****************************************************************************/
PRIVATE int read_register(char reg_addr)
{
	out_byte(CLK_ELE, reg_addr);
	return in_byte(CLK_IO);
}

