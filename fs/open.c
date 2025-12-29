/*************************************************************************//**
 *****************************************************************************
 * @file   fs/open.c
 * The file contains:
 *   - do_open()
 *   - do_close()
 *   - do_lseek()
 *   - create_file()
 * @author Forrest Yu
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

#define MIN_FILE_SECTS 1

PRIVATE struct inode * create_file(char * path, int flags);
PRIVATE int alloc_imap_bit(int dev);
PRIVATE void free_imap_bit(int dev, int inode_nr);
PRIVATE int alloc_smap_bit(int dev, int *nr_sects_to_alloc);
PRIVATE struct inode * new_inode(int dev, int inode_nr, int start_sect, int nr_sects);
PRIVATE void new_dir_entry(struct inode * dir_inode, int inode_nr, char * filename);
PRIVATE int find_free_run(int dev, int smap_blk0_nr, int nr_smap_sects,
		int nr_sects_to_alloc);
PRIVATE void mark_smap_bits(int dev, int smap_blk0_nr, int start_bit,
		int nr_sects_to_alloc);

/*****************************************************************************
 *                                do_open
 *****************************************************************************/
/**
 * Open a file and return the file descriptor.
 * 
 * @return File descriptor if successful, otherwise a negative error code.
 *****************************************************************************/
PUBLIC int do_open()
{
	int fd = -1;		/* return value */

	char pathname[MAX_PATH];

	/* get parameters from the message */
	int flags = fs_msg.FLAGS;	/* access mode */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int src = fs_msg.source;	/* caller proc nr. */
	assert(name_len < MAX_PATH);
	// 由于fs和用户进程分别在ring1和ring3，所在的段不一样，所以不能直接拷贝，而是需要先将逻辑地址转化为线性地址，再进行拷贝
	phys_copy((void*)va2la(TASK_FS, pathname),
		  (void*)va2la(src, fs_msg.PATHNAME),
		  name_len);
	pathname[name_len] = 0;

	/* find a free slot in PROCESS::filp[] */
	int i;
	for (i = 0; i < NR_FILES; i++) {
		if (pcaller->filp[i] == 0) {
			fd = i;
			break;
		}
	}
	if ((fd < 0) || (fd >= NR_FILES))
		panic("filp[] is full (PID:%d)", proc2pid(pcaller));

	/* find a free slot in f_desc_table[] */
	for (i = 0; i < NR_FILE_DESC; i++)
		if (f_desc_table[i].fd_inode == 0)
			break;
	if (i >= NR_FILE_DESC)
		panic("f_desc_table[] is full (PID:%d)", proc2pid(pcaller));

	int inode_nr = search_file(pathname);

	struct inode * pin = 0;

	if (inode_nr == INVALID_INODE) { /* file not exists */
		if (flags & O_CREAT) {
			pin = create_file(pathname, flags);
		}
		else {
			printl("{FS} file not exists: %s\n", pathname);
			return -1;
		}
	}
	else if (flags & O_RDWR) { /* file exists */
		if ((flags & O_CREAT) && (!(flags & O_TRUNC))) {
			assert(flags == (O_RDWR | O_CREAT));
			printl("{FS} file exists: %s\n", pathname);
			return -1;
		}
		assert((flags ==  O_RDWR                     ) ||
		       (flags == (O_RDWR | O_TRUNC          )) ||
		       (flags == (O_RDWR | O_TRUNC | O_CREAT)));

		char filename[MAX_PATH];
		struct inode * dir_inode;
		if (strip_path(filename, pathname, &dir_inode) != 0)
			return -1;
		pin = get_inode(dir_inode->i_dev, inode_nr);
	}
	else { /* file exists, no O_RDWR flag */
		printl("{FS} file exists: %s\n", pathname);
		return -1;
	}

	if (flags & O_TRUNC) {
		assert(pin);
		pin->i_size = 0;
		sync_inode(pin);
	}

	if (pin) {
		/* connects proc with file_descriptor */
		pcaller->filp[fd] = &f_desc_table[i];

		/* connects file_descriptor with inode */
		f_desc_table[i].fd_inode = pin;

		f_desc_table[i].fd_mode = flags;
		f_desc_table[i].fd_cnt = 1;
		f_desc_table[i].fd_pos = 0;

		int imode = pin->i_mode & I_TYPE_MASK;

		if (imode == I_CHAR_SPECIAL) {
			MESSAGE driver_msg;
			driver_msg.type = DEV_OPEN;
			int dev = pin->i_start_sect;
			driver_msg.DEVICE = MINOR(dev);
			assert(MAJOR(dev) == 4);
			assert(dd_map[MAJOR(dev)].driver_nr != INVALID_DRIVER);
			send_recv(BOTH,
				  dd_map[MAJOR(dev)].driver_nr,
				  &driver_msg);
		}
		else if (imode == I_DIRECTORY) {
			assert(pin->i_num == ROOT_INODE);
		}
		else {
			assert(pin->i_mode == I_REGULAR);
		}
	}
	else {
		return -1;
	}

	return fd;
}

/*****************************************************************************
 *                                create_file
 *****************************************************************************/
/**
 * Create a file and return it's inode ptr.
 *
 * @param[in] path   The full path of the new file
 * @param[in] flags  Attribiutes of the new file
 *
 * @return           Ptr to i-node of the new file if successful, otherwise 0.
 * 
 * @see open()
 * @see do_open()
 *****************************************************************************/
PRIVATE struct inode * create_file(char * path, int flags)
{
	char filename[MAX_PATH];
	struct inode * dir_inode;
	if (strip_path(filename, path, &dir_inode) != 0)
		return 0;

	int inode_nr = alloc_imap_bit(dir_inode->i_dev);
	int nr_sects = NR_DEFAULT_FILE_SECTS;
	int free_sect_nr = alloc_smap_bit(dir_inode->i_dev, &nr_sects);
	if (!free_sect_nr) {
		printl("{FS} insufficient space for %s\n", path);
		/* roll back inode allocation */
		free_imap_bit(dir_inode->i_dev, inode_nr);
		return 0;
	}
	struct inode *newino = new_inode(dir_inode->i_dev, inode_nr,
					 free_sect_nr, nr_sects);

	new_dir_entry(dir_inode, newino->i_num, filename);

	return newino;
}

/*****************************************************************************
 *                                do_close
 *****************************************************************************/
/**
 * Handle the message CLOSE.
 * 
 * @return Zero if success.
 *****************************************************************************/
PUBLIC int do_close()
{
	int fd = fs_msg.FD;
	put_inode(pcaller->filp[fd]->fd_inode);
	if (--pcaller->filp[fd]->fd_cnt == 0)
		pcaller->filp[fd]->fd_inode = 0;
	pcaller->filp[fd] = 0;

	return 0;
}

/*****************************************************************************
 *                                do_lseek
 *****************************************************************************/
/**
 * Handle the message LSEEK.
 * 
 * @return The new offset in bytes from the beginning of the file if successful,
 *         otherwise a negative number.
 *****************************************************************************/
PUBLIC int do_lseek()
{
	int fd = fs_msg.FD;
	int off = fs_msg.OFFSET;
	int whence = fs_msg.WHENCE;

	int pos = pcaller->filp[fd]->fd_pos;
	int f_size = pcaller->filp[fd]->fd_inode->i_size;

	switch (whence) {
	case SEEK_SET:
		pos = off;
		break;
	case SEEK_CUR:
		pos += off;
		break;
	case SEEK_END:
		pos = f_size + off;
		break;
	default:
		return -1;
		break;
	}
	if ((pos > f_size) || (pos < 0)) {
		return -1;
	}
	pcaller->filp[fd]->fd_pos = pos;
	return pos;
}

/*****************************************************************************
 *                                alloc_imap_bit
 *****************************************************************************/
/**
 * Allocate a bit in inode-map.
 * 
 * @param dev  In which device the inode-map is located.
 * 
 * @return  I-node nr.
 *****************************************************************************/
PRIVATE int alloc_imap_bit(int dev)
{
	int inode_nr = 0;
	int i, j, k;

	int imap_blk0_nr = 1 + 1; /* 1 boot sector & 1 super block */
	struct super_block * sb = get_super_block(dev);

	for (i = 0; i < sb->nr_imap_sects; i++) {
		RD_SECT(dev, imap_blk0_nr + i);

		for (j = 0; j < SECTOR_SIZE; j++) {
			/* skip `11111111' bytes */
			if (fsbuf[j] == 0xFF)
				continue;
			/* skip `1' bits */
			for (k = 0; ((fsbuf[j] >> k) & 1) != 0; k++) {}
			/* i: sector index; j: byte index; k: bit index */
			inode_nr = (i * SECTOR_SIZE + j) * 8 + k;
			fsbuf[j] |= (1 << k);
			/* write the bit to imap */
			WR_SECT(dev, imap_blk0_nr + i);
			break;
		}

		return inode_nr;
	}

	/* no free bit in imap */
	panic("inode-map is probably full.\n");

	return 0;
}

PRIVATE void free_imap_bit(int dev, int inode_nr)
{
	int imap_blk0_nr = 1 + 1;
	struct super_block * sb = get_super_block(dev);
	int byte_idx = inode_nr / 8;
	int bit_idx = inode_nr % 8;
	int sect = imap_blk0_nr + byte_idx / SECTOR_SIZE;

	if (byte_idx / SECTOR_SIZE >= sb->nr_imap_sects)
		panic("free_imap_bit: invalid inode\n");

	RD_SECT(dev, sect);
	assert(fsbuf[byte_idx % SECTOR_SIZE] & (1 << bit_idx));
	fsbuf[byte_idx % SECTOR_SIZE] &= ~(1 << bit_idx);
	WR_SECT(dev, sect);
}

/*****************************************************************************
 *                                alloc_smap_bit
 *****************************************************************************/
/**
 * Allocate a bit in sector-map.
 * 
 * @param dev  In which device the sector-map is located.
 * @param nr_sects_to_alloc  How many sectors are allocated.
 * 
 * @return  The 1st sector nr allocated.
 *****************************************************************************/
PRIVATE int alloc_smap_bit(int dev, int *nr_sects_to_alloc)
{
	struct super_block * sb = get_super_block(dev);
	int smap_blk0_nr = 1 + 1 + sb->nr_imap_sects;
	int attempt = *nr_sects_to_alloc;
	int desired = attempt;

	if (attempt < MIN_FILE_SECTS)
		attempt = MIN_FILE_SECTS;

	while (attempt >= MIN_FILE_SECTS) {
		int run_start_bit = find_free_run(dev, smap_blk0_nr,
					      sb->nr_smap_sects, attempt);
		if (run_start_bit > 0) {
			mark_smap_bits(dev, smap_blk0_nr, run_start_bit, attempt);
			*nr_sects_to_alloc = attempt;
			if (attempt != desired)
				printl("{FS} alloc_smap_bit: fallback to %d sectors\n",
				       attempt);
			return (run_start_bit - 1) + sb->n_1st_sect;
		}
		attempt >>= 1;
	}

	return 0;
}

/*****************************************************************************
 *                                new_inode
 *****************************************************************************/
/**
 * Generate a new i-node and write it to disk.
 * 
 * @param dev  Home device of the i-node.
 * @param inode_nr  I-node nr.
 * @param start_sect  Start sector of the file pointed by the new i-node.
 * 
 * @return  Ptr of the new i-node.
 *****************************************************************************/
PRIVATE struct inode * new_inode(int dev, int inode_nr, int start_sect, int nr_sects)
{
	struct inode * new_inode = get_inode(dev, inode_nr);

	new_inode->i_mode = I_REGULAR;
	new_inode->i_size = 0;
	new_inode->i_start_sect = start_sect;
	new_inode->i_nr_sects = nr_sects;

	new_inode->i_dev = dev;
	new_inode->i_cnt = 1;
	new_inode->i_num = inode_nr;

	/* write to the inode array */
	sync_inode(new_inode);

	return new_inode;
}

PRIVATE int find_free_run(int dev, int smap_blk0_nr, int nr_smap_sects,
		int nr_sects_to_alloc)
{
	int run_start_bit = 0;
	int run_len = 0;
	int i;
	int j;
	int k;

	for (i = 0; i < nr_smap_sects; i++) {
		RD_SECT(dev, smap_blk0_nr + i);
		for (j = 0; j < SECTOR_SIZE; j++) {
			u8 byte = fsbuf[j];
			if (byte == 0xFF) {
				run_len = 0;
				run_start_bit = 0;
				continue;
			}
			for (k = 0; k < 8; k++) {
				if ((byte >> k) & 1) {
					run_len = 0;
					run_start_bit = 0;
					continue;
				}
				if (run_len == 0) {
					int absolute_bit = (i * SECTOR_SIZE + j) * 8 + k;
					run_start_bit = absolute_bit + 1;
				}
				run_len++;
				if (run_len == nr_sects_to_alloc)
					return run_start_bit;
			}
		}
	}

	return 0;
}

PRIVATE void mark_smap_bits(int dev, int smap_blk0_nr, int start_bit,
		int nr_sects_to_alloc)
{
	int bits_to_mark = nr_sects_to_alloc;
	int current_bit = start_bit - 1;
	while (bits_to_mark > 0) {
		int sect_offset = current_bit / (SECTOR_SIZE * 8);
		int bit_in_sect = current_bit % (SECTOR_SIZE * 8);
		int byte_idx = bit_in_sect / 8;
		int bit_idx = bit_in_sect % 8;

		RD_SECT(dev, smap_blk0_nr + sect_offset);
		for (; byte_idx < SECTOR_SIZE && bits_to_mark > 0; byte_idx++) {
			while (bit_idx < 8 && bits_to_mark > 0) {
				assert(((fsbuf[byte_idx] >> bit_idx) & 1) == 0);
				fsbuf[byte_idx] |= (1 << bit_idx);
				current_bit++;
				bits_to_mark--;
				bit_idx++;
			}
			bit_idx = 0;
		}
		WR_SECT(dev, smap_blk0_nr + sect_offset);
	}
}

/*****************************************************************************
 *                                new_dir_entry
 *****************************************************************************/
/**
 * Write a new entry into the directory.
 * 
 * @param dir_inode  I-node of the directory.
 * @param inode_nr   I-node nr of the new file.
 * @param filename   Filename of the new file.
 *****************************************************************************/
PRIVATE void new_dir_entry(struct inode *dir_inode,int inode_nr,char *filename)
{
	/* write the dir_entry */
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE) / SECTOR_SIZE;
	int nr_dir_entries =
		dir_inode->i_size / DIR_ENTRY_SIZE; /**
						     * including unused slots
						     * (the file has been
						     * deleted but the slot
						     * is still there)
						     */
	int m = 0;
	struct dir_entry * pde;
	struct dir_entry * new_de = 0;

	int i, j;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);

		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			if (++m > nr_dir_entries)
				break;

			if (pde->inode_nr == 0) { /* it's a free slot */
				new_de = pde;
				break;
			}
		}
		if (m > nr_dir_entries ||/* all entries have been iterated or */
		    new_de)              /* free slot is found */
			break;
	}
	if (!new_de) { /* reached the end of the dir */
		new_de = pde;
		dir_inode->i_size += DIR_ENTRY_SIZE;
	}
	new_de->inode_nr = inode_nr;
	strcpy(new_de->name, filename);

	/* write dir block -- ROOT dir block */
	WR_SECT(dir_inode->i_dev, dir_blk0_nr + i);

	/* update dir inode */
	sync_inode(dir_inode);
}
