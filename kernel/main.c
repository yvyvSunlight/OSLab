/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            main.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

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

#define SHELLS_PER_TTY 2


/*****************************************************************************
 *                               kernel_main
 *****************************************************************************/
/**
 * jmp from kernel.asm::_start. 
 * 
 *****************************************************************************/
PUBLIC int kernel_main()
{
	disp_str("\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

	int i, j, eflags, prio;
    u8  rpl;
    u8  priv; /* privilege */

	struct task * t;
	struct proc * p = proc_table;

	char * stk = task_stack + STACK_SIZE_TOTAL;

	#define TASK_LOG_INDEX 5 

	// 系统任务和NATIVE用户进程
	for (i = 0; i < NR_TASKS + NR_PROCS; i++,p++,t++) {
		if (i >= NR_TASKS + NR_NATIVE_PROCS) {
			p->p_flags = FREE_SLOT;
			continue;
		}

        if (i < NR_TASKS) {     /* TASK - 内核任务 */
                t	= task_table + i;
                priv	= PRIVILEGE_TASK;
                rpl     = RPL_TASK;
                eflags  = 0x1202;/* IF=1, IOPL=1, bit 2 is always 1 */
				
				// LOG 任务设置最低优先级
				if (i == TASK_LOG_INDEX) {
					prio = 1;
				} else {
					prio = 15;
				}
        }
        else {                  /* USER PROC - 用户进程 */
                t	= user_proc_table + (i - NR_TASKS);
                priv	= PRIVILEGE_USER;
                rpl     = RPL_USER;
                eflags  = 0x202;	/* IF=1, bit 2 is always 1 */
				prio    = 5;  // 用户进程保持中优先级
        }

		strcpy(p->name, t->name);	/* name of the process */
		p->p_parent = NO_TASK;

		if (strcmp(t->name, "INIT") != 0) {
			p->ldts[INDEX_LDT_C]  = gdt[SELECTOR_KERNEL_CS >> 3];
			p->ldts[INDEX_LDT_RW] = gdt[SELECTOR_KERNEL_DS >> 3];

			/* change the DPLs */
			p->ldts[INDEX_LDT_C].attr1  = DA_C   | priv << 5;
			p->ldts[INDEX_LDT_RW].attr1 = DA_DRW | priv << 5;
		}
		else {		/* INIT process */
			unsigned int k_base;
			unsigned int k_limit;
			int ret = get_kernel_map(&k_base, &k_limit);
			assert(ret == 0);
			init_desc(&p->ldts[INDEX_LDT_C],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_C | priv << 5);

			init_desc(&p->ldts[INDEX_LDT_RW],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_DRW | priv << 5);
		}

		p->regs.cs = INDEX_LDT_C << 3 |	SA_TIL | rpl;
		p->regs.ds =
			p->regs.es =
			p->regs.fs =
			p->regs.ss = INDEX_LDT_RW << 3 | SA_TIL | rpl;
		p->regs.gs = (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;
		p->regs.eip	= (u32)t->initial_eip;
		p->regs.esp	= (u32)stk;
		p->regs.eflags	= eflags;

		/* Record stack bounds for TASK/NATIVE (linear addresses) */
		p->stack_high = (u32)stk;              /* initial esp (high end) */
		p->stack_low  = (u32)(stk - t->stacksize); /* low end */

		// ========== 关键改动2：确认优先级赋值（原有逻辑保留，已通过上面的 prio 变量修改） ==========
		p->ticks = p->priority = prio;
		p->queue_level = 0;

		p->p_flags = 0;
		p->p_msg = 0;
		p->p_recvfrom = NO_TASK;
		p->p_sendto = NO_TASK;
		p->has_int_msg = 0;
		p->q_sending = 0;
		p->next_sending = 0;

		for (j = 0; j < NR_FILES; j++)
			p->filp[j] = 0;

		stk -= t->stacksize;
	}

	k_reenter = 0;
	ticks = 0;

	p_proc_ready	= proc_table;

	init_clock();
    init_keyboard();

	restart();

	while(1){}
}


/*****************************************************************************
 *                                get_ticks
 *****************************************************************************/
PUBLIC int get_ticks()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = GET_TICKS;
	send_recv(BOTH, TASK_SYS, &msg);
	return msg.RETVAL;
}


/**
 * @struct posix_tar_header
 * Borrowed from GNU `tar'
 */
struct posix_tar_header
{				/* byte offset */
	char name[100];		/*   0 */
	char mode[8];		/* 100 */
	char uid[8];		/* 108 */
	char gid[8];		/* 116 */
	char size[12];		/* 124 */
	char mtime[12];		/* 136 */
	char chksum[8];		/* 148 */
	char typeflag;		/* 156 */
	char linkname[100];	/* 157 */
	char magic[6];		/* 257 */
	char version[2];	/* 263 */
	char uname[32];		/* 265 */
	char gname[32];		/* 297 */
	char devmajor[8];	/* 329 */
	char devminor[8];	/* 337 */
	char prefix[155];	/* 345 */
	/* 500 */
};

PRIVATE u8 popcount8(u8 b)
{
	u8 cnt = 0;
	while (b) {
		cnt += b & 0x1;
		b >>= 1;
	}
	return cnt;
}

PRIVATE u8 accumulate_checksum(const u8 * buf, int len, u8 seed)
{
	for (int i = 0; i < len; i++) {
		seed ^= popcount8(buf[i]);
	}
	return seed;
}

PRIVATE int should_skip_checksum(const char * name)
{
	return strcmp(name, "kernel.bin") == 0 ||
	       strcmp(name, "hdboot.bin") == 0 ||
	       strcmp(name, "hdloader.bin") == 0;
}

// 计算已打开的文件的奇偶校验和
PRIVATE int calc_checksum_fd(int fd, int total_len)
{
	u8 buf[SECTOR_SIZE];
	int left = total_len;
	u8 checksum = 0;

	if (lseek(fd, 0, SEEK_SET) < 0)
		return -1;

	while (left > 0) {
		int to_read = min((int)sizeof(buf), left);
		int r = read(fd, buf, to_read);
		if (r != to_read)
			return -1;
		checksum = accumulate_checksum(buf, r, checksum);
		left -= r;
	}

	/* rewind for callers that reuse the fd */
	lseek(fd, 0, SEEK_SET);

	return checksum;
}

/*****************************************************************************
 *                                untar
 *****************************************************************************/
/**
 * Extract the tar file and store them.
 * 
 * @param filename The tar file.
 *****************************************************************************/
void untar(const char * filename)
{
	printf("[extract `%s'\n", filename);
	int fd = open(filename, O_RDWR);
	assert(fd != -1);

	// 缓冲区，16个扇区
	char buf[SECTOR_SIZE * 16];
	int chunk = sizeof(buf);
	int i = 0;
	int bytes = 0;

	while (1) {
		// header block为512字节
		bytes = read(fd, buf, SECTOR_SIZE);
		assert(bytes == SECTOR_SIZE); 

		if (buf[0] == 0) {
			if (i == 0)
				printf("    need not unpack the file.\n");
			break;
		}
		i++;

		struct posix_tar_header * phdr = (struct posix_tar_header *)buf;
		char name_bak[105];
		memset(name_bak, 0, sizeof(name_bak));
		memcpy(name_bak, phdr->name, 100); /* tar name field is 100 bytes */

		/* calculate the file size */
		char * p = phdr->size;
		int f_len = 0;
		while (*p)
			f_len = (f_len * 8) + (*p++ - '0'); /* octal */

		int bytes_left = f_len;
		int fdout = open(phdr->name, O_CREAT | O_RDWR | O_TRUNC);
		if (fdout == -1) {
			printf("    failed to extract file: %s\n", phdr->name);
			printf(" aborted]\n");
			close(fd);
			return;
		}
		printf("    %s\n", phdr->name);
		u8 checksum = 0;
		int need_checksum = !should_skip_checksum(phdr->name);
		while (bytes_left) {
			int iobytes = min(chunk, bytes_left);
			read(fd, buf,
			     ((iobytes - 1) / SECTOR_SIZE + 1) * SECTOR_SIZE);
			// 将压缩包的数据内容写到同名文件中 => 解压
			bytes = write(fdout, buf, iobytes);
			assert(bytes == iobytes);
			if (need_checksum)
				checksum = accumulate_checksum((u8*)buf, iobytes, checksum);
			bytes_left -= iobytes;
		}
		close(fdout);
		// 将奇偶校验值写入
		if (need_checksum) {
			int ret = set_checksum(name_bak, checksum);
			if (ret != 0)
				printf("     for %s\n", name_bak);
		}
	}

	if (i) {
		lseek(fd, 0, SEEK_SET);
		buf[0] = 0;
		bytes = write(fd, buf, 1);
		assert(bytes == 1);
	}

	close(fd);

	printf(" done, %d files extracted]\n", i);
}

/*****************************************************************************
 *                                shabby_shell
 *****************************************************************************/
/**
 * A very very simple shell.
 * 
 * @param tty_name  TTY file name.
 *****************************************************************************/
void shabby_shell(const char * tty_name)
{
	// 0和1与输入输出是通过open顺序来绑定的
	int fd_stdin  = open(tty_name, O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);

	char rdbuf[128];

	while (1) {
		int r = read(0, rdbuf, 70);
		
		if (r <= 0)  continue;
		// trim trailing LF
		while (r > 0 && rdbuf[r - 1] == '\n') 
			r--;
		rdbuf[r] = 0;

		int argc = 0;
		char * argv[PROC_ORIGIN_STACK];
		char * p = rdbuf;
		char * s;
		int word = 0;
		char ch;
		do {
			ch = *p;
			if (*p != ' ' && *p != 0 && !word) {
				s = p;
				word = 1;
			}
			if ((*p == ' ' || *p == 0) && word) {
				word = 0;
				argv[argc++] = s;
				*p = 0;
			}
			p++;
		} while(ch);
		argv[argc] = 0;

		int fd = open(argv[0], O_RDWR);  // 拿到命令文件的文件描述符
		if (fd == -1) {
			if (rdbuf[0]) {
				write(1, "{", 1);
				write(1, rdbuf, r);
				write(1, "}\n", 2);
			}
		}
		// 如果找到命令，就先做校验，通过后fork执行
		else {
			struct stat s;
			int verified = 0;
			int stored = get_checksum(argv[0]);
			if (stored >= 0 && stat(argv[0], &s) == 0) {
				int current = calc_checksum_fd(fd, s.st_size);
				if (current >= 0 && stored == current)
					verified = 1;
			}

			if (!verified) {
				printf("[checksum failed] %s\n", argv[0]);
				close(fd);
				continue;
			}

			printf("[checksum ok] %s\n", argv[0]);
			close(fd);

			int pid = fork();
			if (pid != 0) { /* parent */
				int s;
				wait(&s);
			}
			else {/* child */
				execv(argv[0], argv);
			}
		}
	}

	close(1);
	close(0);
}

/*****************************************************************************
 *                                Init
 *****************************************************************************/
/**
 * The hen.
 * 
 *****************************************************************************/
void Init()
{
	int fd_stdin  = open("/dev_tty0", O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open("/dev_tty0", O_RDWR);
	assert(fd_stdout == 1);

	printf("Init() is running ...\n");

	/* extract `cmd.tar' */
	untar("/cmd.tar");
			

	char * tty_list[] = {"/dev_tty1", "/dev_tty2"};
	const int shell_slots = SHELLS_PER_TTY;

	int i;
	for (i = 0; i < sizeof(tty_list) / sizeof(tty_list[0]); i++) {
		int shell_idx;
		for (shell_idx = 0; shell_idx < shell_slots; shell_idx++) {
			int pid = fork();
			if (pid != 0) { /* parent process */
				printf("[parent spawned shell %d on %s, child pid:%d]\n",
					shell_idx, tty_list[i], pid);
			}
			else { /* child process */
				printf("[child is running, pid:%d, tty:%s slot:%d]\n",
					getpid(), tty_list[i], shell_idx);
				// 关闭占用的输入输出，在shabby_shell内部重新绑定
				close(fd_stdin);
				close(fd_stdout);
				
				shabby_shell(tty_list[i]);
				assert(0);
			}
		}
	}

	while (1) {
		int s;
		int child = wait(&s);
		printf("child (%d) exited with status: %d.\n", child, s);
	}

	assert(0);
}


/*======================================================================*
                               TestA
 *======================================================================*/
void TestA()
{
	// for add breakpoint
	int a = 0;
	for(;;);
}

/*======================================================================*
                               TestB
 *======================================================================*/
void TestB()
{
	for(;;);
}

/*======================================================================*
                               TestB
 *======================================================================*/
void TestC()
{
	int c = 0;
	for(;;);
}

/*****************************************************************************
 *                                panic
 *****************************************************************************/
PUBLIC void panic(const char *fmt, ...)
{
	int i;
	char buf[256];

	/* 4 is the size of fmt in the stack */
	va_list arg = (va_list)((char*)&fmt + 4);

	i = vsprintf(buf, fmt, arg);

	printl("%c !!panic!! %s", MAG_CH_PANIC, buf);

	/* should never arrive here */
	__asm__ __volatile__("ud2");
}

