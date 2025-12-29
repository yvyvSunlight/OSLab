
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
				tty.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
						    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _ORANGES_TTY_H_
#define _ORANGES_TTY_H_


#define TTY_IN_BYTES		256	/* tty input queue size */
#define TTY_OUT_BUF_LEN		2	/* tty output buffer size */
#define TTY_PENDING_READS	16	/* max concurrent blocked readers per tty */

struct s_tty;
struct s_console;

typedef struct s_tty_read_req {
	int	caller;
	int	proc_nr;
	void*	req_buf;
	int	count;
	int	transferred;
	int	shell_id;
} TTY_READ_REQ;

typedef struct s_tty_shell_slot {
	int	proc_nr;
	int	shell_id;
} TTY_SHELL_SLOT;

/* TTY */
typedef struct s_tty
{
	u32	ibuf[TTY_IN_BYTES];	/* TTY input buffer */
	u32*	ibuf_head;		/* the next free slot */
	u32*	ibuf_tail;		/* the val to be processed by TTY */
	int	ibuf_cnt;		/* how many */

	int	tty_caller;
	int	tty_procnr;
	void*	tty_req_buf;
	int	tty_left_cnt;
	int	tty_trans_cnt;

	TTY_READ_REQ	pending_reads[TTY_PENDING_READS];
	int	pending_head;
	int	pending_tail;
	int	pending_count;

	TTY_SHELL_SLOT	shell_slots[TTY_PENDING_READS];
	int	shell_slot_count;
	int	next_shell_id;
	int	active_shell_id;
	int	current_shell_id;

	struct s_console *	console;
}TTY;

#endif /* _ORANGES_TTY_H_ */
