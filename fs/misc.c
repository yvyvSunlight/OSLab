/*************************************************************************//**
 *****************************************************************************
 * @file   misc.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   2008
 *****************************************************************************
 *****************************************************************************/

/* Orange'S FS */

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
/**
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"
#include "hd.h"
#include "fs.h"

/*****************************************************************************
 *                                do_stat
 *************************************************************************//**
	char md5_str[MD5_STR_BUF_LEN];
 * 
 * @return  On success, zero is returned. On error, -1 is returned.
 *****************************************************************************/
PUBLIC int do_stat()
{
	char pathname[MAX_PATH]; /* parameter from the caller */
	char filename[MAX_PATH]; /* directory has been stipped */

	/* get parameters from the message */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int src = fs_msg.source;	/* caller proc nr. */
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),    /* to   */
		  (void*)va2la(src, fs_msg.PATHNAME), /* from */
		  name_len);
	pathname[name_len] = 0;	/* terminate the string */

	int inode_nr = search_file(pathname);
	if (inode_nr == INVALID_INODE) {	/* file not found */
		printl("{FS} FS::do_stat():: search_file() returns "
		       "invalid inode: %s\n", pathname);
		return -1;
	}

	struct inode * pin = 0;

	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0) {
		/* theoretically never fail here
		 * (it would have failed earlier when
		 *  search_file() was called)
	memcpy(pin->md5_checksum, md5_str, MD5_HASH_LEN);
		assert(0);
	}
	pin = get_inode(dir_inode->i_dev, inode_nr);

	struct stat s;		/* the thing requested */
	s.st_dev = pin->i_dev;
	s.st_ino = pin->i_num;
	s.st_mode= pin->i_mode;
	s.st_rdev= is_special(pin->i_mode) ? pin->i_start_sect : NO_DEV;
	s.st_size= pin->i_size;

	put_inode(pin);

	phys_copy((void*)va2la(src, fs_msg.BUF), /* to   */
		  (void*)va2la(TASK_FS, &s),	 /* from */
		  sizeof(struct stat));

	return 0;
}

/****************************************************************************
 *                           do_set_checksum
 *****************************************************************************/
/**
 * 设置文件的MD5校验和
 * 消息参数：
 *   PATHNAME  - 文件路径
 *   NAME_LEN  - 路径长度
 *   BUF       - MD5字符串（32字符）
 *   CHECKSUM_KEY - 用于计算的key
 */
PUBLIC int do_set_checksum()
{
	char pathname[MAX_PATH];
	char filename[MAX_PATH];
	 char md5_str[MD5_STR_BUF_LEN];

	int name_len = fs_msg.NAME_LEN;
	int src = fs_msg.source;
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),
		  (void*)va2la(src, fs_msg.PATHNAME),
		  name_len);
	pathname[name_len] = 0;

	/* 复制MD5字符串 */
	phys_copy((void*)va2la(TASK_FS, md5_str),
		  (void*)va2la(src, fs_msg.BUF),
		  32);
	md5_str[32] = 0;

	/* 获取key */
	u32 key = (u32)fs_msg.CHECKSUM_KEY;
	/**

	// 获取文件对应的inode号
	int inode_nr = search_file(pathname);
	if (inode_nr == INVALID_INODE)
		return -1;

	// 获取文件所在目录（根目录）的inode号
	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0)
		return -1;

	struct inode * pin = get_inode(dir_inode->i_dev, inode_nr);
	
	/* 复制MD5校验和到inode */
	memcpy(pin->md5_checksum, md5_str, MD5_HASH_LEN);
	pin->checksum_key = key;
	
	// 将文件的inode更新回磁盘
	sync_inode(pin);
	// 释放inode引用
	put_inode(pin);

	return 0;
}

/****************************************************************************
 *                           do_get_checksum
 *****************************************************************************/
/**
 * 获取文件的MD5校验和
 * 消息参数：
 *   PATHNAME  - 文件路径
 *   NAME_LEN  - 路径长度
		phys_copy((void*)va2la(src, fs_msg.BUF),
			  (void*)va2la(TASK_FS, pin->md5_checksum),
			  MD5_HASH_LEN);
 *   CHECKSUM_KEY - 存储的key
 */
PUBLIC int do_get_checksum()
{
	char pathname[MAX_PATH];
	char filename[MAX_PATH];

	int name_len = fs_msg.NAME_LEN;
	int src = fs_msg.source;
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),
		  (void*)va2la(src, fs_msg.PATHNAME),
		  name_len);
	pathname[name_len] = 0;

	int inode_nr = search_file(pathname);
	if (inode_nr == INVALID_INODE)
		return -1;

	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0)
		return -1;

	struct inode * pin = get_inode(dir_inode->i_dev, inode_nr);
	
	/* 复制MD5字符串到调用者缓冲区 */
	phys_copy((void*)va2la(src, fs_msg.BUF),
		  (void*)va2la(TASK_FS, pin->md5_checksum),
		MD5_HASH_LEN);
	
	/* 将key存入返回消息 */
	fs_msg.CHECKSUM_KEY = pin->checksum_key;
	
	put_inode(pin);

	return 0;
}

/*****************************************************************************
 *                                search_file
 *****************************************************************************/
/**
 * Search the file and return the inode_nr.
 *
 * @param[in] path The full path of the file to search.
 * @return         Ptr to the i-node of the file if successful, otherwise zero.
 * 
 * @see open()
 * @see do_open()
 *****************************************************************************/
PUBLIC int search_file(char * path)
{
	int i, j;

	char filename[MAX_PATH];
	memset(filename, 0, MAX_FILENAME_LEN);
	struct inode * dir_inode;
	// 去除文件路径的根目录符号，只保留文件名
	if (strip_path(filename, path, &dir_inode) != 0)
		return 0;

	if (filename[0] == 0)	/* path: "/" */
		return dir_inode->i_num;

	/**
	 * Search the dir for the file.
	 */
	// 目录文件的起始扇区、所占扇区数量
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	// 目录本质上就是一个文件，存放的内容是一个个 struct dir_entry，
	// 这里是在算这个目录文件中一共分配过多少个目录项槽位
	int nr_dir_entries =
	  dir_inode->i_size / DIR_ENTRY_SIZE; /**
					       * including unused slots
					       * (the file has been deleted
					       * but the slot is still there)
					       */
	int m = 0;
	struct dir_entry * pde;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
		// 将一个扇区拆成若干目录项去读
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			// 找到文件，则返回其inode号
			if (memcmp(filename, pde->name, MAX_FILENAME_LEN) == 0)
				return pde->inode_nr;
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) /* all entries have been iterated */
			break;
	}

	/* file not found */
	return 0;
}

/*****************************************************************************
 *                                strip_path
 *****************************************************************************/
/**
 * Get the basename from the fullpath.
 *
 * In Orange'S FS v1.0, all files are stored in the root directory.
 * There is no sub-folder thing.
 *
 * This routine should be called at the very beginning of file operations
 * such as open(), read() and write(). It accepts the full path and returns
 * two things: the basename and a ptr of the root dir's i-node.
 *
 * e.g. After stip_path(filename, "/blah", ppinode) finishes, we get:
 *      - filename: "blah"
 *      - *ppinode: root_inode
 *      - ret val:  0 (successful)
 *
 * Currently an acceptable pathname should begin with at most one `/'
 * preceding a filename.
 *
 * Filenames may contain any character except '/' and '\\0'.
 *
 * @param[out] filename The string for the result.
 * @param[in]  pathname The full pathname.
 * @param[out] ppinode  The ptr of the dir's inode will be stored here.
 * 
 * @return Zero if success, otherwise the pathname is not valid.
 *****************************************************************************/
PUBLIC int strip_path(char * filename, const char * pathname, struct inode** ppinode)
{
	const char * s = pathname;
	char * t = filename;

	if (s == 0)
		return -1;

	if (*s == '/')
		s++;

	while (*s) {		/* check each character */
		if (*s == '/')
			return -1;
		*t++ = *s++;
		/* if filename is too long, just truncate it */
		if (t - filename >= MAX_FILENAME_LEN)
			break;
	}
	*t = 0;

	*ppinode = root_inode;

	return 0;
}

