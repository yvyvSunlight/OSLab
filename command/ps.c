#include "stdio.h"
#include "string.h"
#include "const.h"

#define MAX_PS 64

static const char *state_to_text(int flags)
{
	if (flags == 0)
		return "RUN";
	if (flags & HANGING)
		return "HANG";
	if (flags & WAITING)
		return "WAIT";
	if (flags & SENDING)
		return "SEND";
	if (flags & RECEIVING)
		return "RECV";
	return "BLK";
}

int main(void)
{
	struct proc_info entries[MAX_PS];
	int count = get_procs(entries, MAX_PS);
	int i;

	if (count < 0) {
		printf("ps: syscall failed\n");
		return 1;
	}

	printf("PID PPID STAT Q TICKS PRI NAME\n");
	for (i = 0; i < count; i++) {
		struct proc_info *info = &entries[i];
		printf("%d %d %s %d %d %d %s\n",
		       info->pid,
		       info->parent_pid,
		       state_to_text(info->flags),
		       info->queue_level,
		       info->ticks,
		       info->priority,
		       info->name);
	}

	return 0;
}
