/*************************************************************************//**
 *****************************************************************************
 * @file   link.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   Tue Jun  3 17:05:10 2008
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


/*****************************************************************************
 *                                do_unlink
 *****************************************************************************/
/**
 * Remove a file.
 *
 * @note We clear the i-node in inode_array[] although it is not really needed.
 *       We don't clear the data bytes so the file is recoverable.
 * 
 * @return On success, zero is returned.  On error, -1 is returned.
 *****************************************************************************/
PUBLIC int do_unlink()
{
	char pathname[MAX_PATH];

	/* get parameters from the message */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int src = fs_msg.source;	/* caller proc nr. */
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),
		  (void*)va2la(src, fs_msg.PATHNAME),
		  name_len);
	pathname[name_len] = 0;

	if (strcmp(pathname , "/") == 0) {
		printl("{FS} FS:do_unlink():: cannot unlink the root\n");
		return -1;
	}

	int inode_nr = search_file(pathname);
	if (inode_nr == INVALID_INODE) {	/* file not found */
		printl("{FS} FS::do_unlink():: search_file() returns "
			"invalid inode: %s\n", pathname);
		return -1;
	}

	char filename[MAX_PATH];
	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0)
		return -1;

	struct inode * pin = get_inode(dir_inode->i_dev, inode_nr);

	if (pin->i_mode != I_REGULAR) { /* can only remove regular files */
		printl("{FS} cannot remove file %s, because "
		       "it is not a regular file.\n",
		       pathname);
		return -1;
	}

	if (pin->i_cnt > 1) {	/* the file was opened */
		printl("{FS} cannot remove file %s, because pin->i_cnt is %d.\n",
		       pathname, pin->i_cnt);
		return -1;
	}

	struct super_block * sb = get_super_block(pin->i_dev);

	/*************************/
	/* free the bit in i-map */
	/*************************/
	int byte_idx = inode_nr / 8;
	int bit_idx = inode_nr % 8;
	assert(byte_idx < SECTOR_SIZE);	/* we have only one i-map sector */
	/* read sector 2 (skip bootsect and superblk): */
	RD_SECT(pin->i_dev, 2);
	assert(fsbuf[byte_idx % SECTOR_SIZE] & (1 << bit_idx));
	fsbuf[byte_idx % SECTOR_SIZE] &= ~(1 << bit_idx);
	WR_SECT(pin->i_dev, 2);

	/**************************/
	/* free the bits in s-map */
	/**************************/
	int smap_blk0_nr = 2 + sb->nr_imap_sects;
	int start_bit = pin->i_start_sect - sb->n_1st_sect;
	int bits_to_free = pin->i_nr_sects;

	if (start_bit < 0 || bits_to_free <= 0)
		panic("invalid smap release request\n");

	while (bits_to_free > 0) {
		int sect_offset = start_bit / (SECTOR_SIZE * 8);
		int bit_in_sect = start_bit % (SECTOR_SIZE * 8);
		int bits_in_sector = min(bits_to_free, (SECTOR_SIZE * 8) - bit_in_sect);
		int byte_idx_smap = bit_in_sect / 8;
		int bit_idx_smap = bit_in_sect % 8;
		int remaining = bits_in_sector;

		RD_SECT(pin->i_dev, smap_blk0_nr + sect_offset);

		while (remaining > 0) {
			if (byte_idx_smap >= SECTOR_SIZE) {
				WR_SECT(pin->i_dev, smap_blk0_nr + sect_offset);
				sect_offset++;
				byte_idx_smap = 0;
				bit_idx_smap = 0;
				RD_SECT(pin->i_dev, smap_blk0_nr + sect_offset);
			}

			if ((fsbuf[byte_idx_smap] >> bit_idx_smap) & 1)
				fsbuf[byte_idx_smap] &= ~(1 << bit_idx_smap);

			bit_idx_smap++;
			if (bit_idx_smap == 8) {
				bit_idx_smap = 0;
				byte_idx_smap++;
			}

			start_bit++;
			bits_to_free--;
			remaining--;
		}

		WR_SECT(pin->i_dev, smap_blk0_nr + sect_offset);
	}

	/***************************/
	/* clear the i-node itself */
	/***************************/
	pin->i_mode = 0;
	pin->i_size = 0;
	pin->i_start_sect = 0;
	pin->i_nr_sects = 0;
	sync_inode(pin);
	/* release slot in inode_table[] */
	put_inode(pin);

	/************************************************/
	/* set the inode-nr to 0 in the directory entry */
	/************************************************/
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE) / SECTOR_SIZE;
	int nr_dir_entries =
		dir_inode->i_size / DIR_ENTRY_SIZE; /* including unused slots
						     * (the file has been
						     * deleted but the slot
						     * is still there)
						     */
	int m = 0;
	struct dir_entry * pde = 0;
	int flg = 0;
	int dir_size = 0;
	int i;

	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);

		pde = (struct dir_entry *)fsbuf;
		int j;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			if (++m > nr_dir_entries)
				break;

			if (pde->inode_nr == inode_nr) {
				/* pde->inode_nr = 0; */
				memset(pde, 0, DIR_ENTRY_SIZE);
				WR_SECT(dir_inode->i_dev, dir_blk0_nr + i);
				flg = 1;
				break;
			}

			if (pde->inode_nr != INVALID_INODE)
				dir_size += DIR_ENTRY_SIZE;
		}

		if (m > nr_dir_entries || /* all entries have been iterated OR */
		    flg) /* file is found */
			break;
	}
	assert(flg);
	if (m == nr_dir_entries) { /* the file is the last one in the dir */
		dir_inode->i_size = dir_size;
		sync_inode(dir_inode);
	}

	return 0;
}
