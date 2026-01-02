/***************************************************************************
 ****************************************************************************
 * @file   misc.c
 * @brief  FS helpers + checksum helpers (key stays inside FS)
 * @author Forrest Y. Yu
 * @date   2008
 ****************************************************************************
 *****************************************************************************/

/* Orange'S FS */

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
#include "hd.h"

/* ============================================================
 * checksum key: FS private (never stored or returned)
 * ============================================================ */
PRIVATE int s_ck_inited = 0;
PRIVATE u32 s_ck_key = 0;

PRIVATE void ensure_checksum_key_inited(void)
{
	if (!s_ck_inited) {
		init_timestamp();
		s_ck_key = generate_checksum_key();
		s_ck_inited = 1;
	}
}

/* ============================================================
 * MD5 实现（复制内核版本，符号保持 PRIVATE）
 * ============================================================ */
typedef struct {
	u32 state[4];
	u32 count[2];
	u8  buffer[64];
} MD5_CTX;

#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

#define FF(a, b, c, d, x, s, ac) { \
	(a) += F((b), (c), (d)) + (x) + (u32)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
}
#define GG(a, b, c, d, x, s, ac) { \
	(a) += G((b), (c), (d)) + (x) + (u32)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
}
#define HH(a, b, c, d, x, s, ac) { \
	(a) += H((b), (c), (d)) + (x) + (u32)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
}
#define II(a, b, c, d, x, s, ac) { \
	(a) += I((b), (c), (d)) + (x) + (u32)(ac); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
}

PRIVATE u8 PADDING[64] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

PRIVATE void md5_encode(u8 *output, u32 *input, unsigned int len)
{
	unsigned int i, j;
	for (i = 0, j = 0; j < len; i++, j += 4) {
		output[j]     = (u8)(input[i] & 0xff);
		output[j + 1] = (u8)((input[i] >> 8) & 0xff);
		output[j + 2] = (u8)((input[i] >> 16) & 0xff);
		output[j + 3] = (u8)((input[i] >> 24) & 0xff);
	}
}

PRIVATE void md5_decode(u32 *output, u8 *input, unsigned int len)
{
	unsigned int i, j;
	for (i = 0, j = 0; j < len; i++, j += 4) {
		output[i] = ((u32)input[j]) |
			    (((u32)input[j + 1]) << 8) |
			    (((u32)input[j + 2]) << 16) |
			    (((u32)input[j + 3]) << 24);
	}
}

PRIVATE void md5_transform(u32 state[4], u8 block[64])
{
	u32 a = state[0], b = state[1], c = state[2], d = state[3], x[16];

	md5_decode(x, block, 64);

	FF(a, b, c, d, x[ 0], S11, 0xd76aa478);
	FF(d, a, b, c, x[ 1], S12, 0xe8c7b756);
	FF(c, d, a, b, x[ 2], S13, 0x242070db);
	FF(b, c, d, a, x[ 3], S14, 0xc1bdceee);
	FF(a, b, c, d, x[ 4], S11, 0xf57c0faf);
	FF(d, a, b, c, x[ 5], S12, 0x4787c62a);
	FF(c, d, a, b, x[ 6], S13, 0xa8304613);
	FF(b, c, d, a, x[ 7], S14, 0xfd469501);
	FF(a, b, c, d, x[ 8], S11, 0x698098d8);
	FF(d, a, b, c, x[ 9], S12, 0x8b44f7af);
	FF(c, d, a, b, x[10], S13, 0xffff5bb1);
	FF(b, c, d, a, x[11], S14, 0x895cd7be);
	FF(a, b, c, d, x[12], S11, 0x6b901122);
	FF(d, a, b, c, x[13], S12, 0xfd987193);
	FF(c, d, a, b, x[14], S13, 0xa679438e);
	FF(b, c, d, a, x[15], S14, 0x49b40821);

	GG(a, b, c, d, x[ 1], S21, 0xf61e2562);
	GG(d, a, b, c, x[ 6], S22, 0xc040b340);
	GG(c, d, a, b, x[11], S23, 0x265e5a51);
	GG(b, c, d, a, x[ 0], S24, 0xe9b6c7aa);
	GG(a, b, c, d, x[ 5], S21, 0xd62f105d);
	GG(d, a, b, c, x[10], S22, 0x02441453);
	GG(c, d, a, b, x[15], S23, 0xd8a1e681);
	GG(b, c, d, a, x[ 4], S24, 0xe7d3fbc8);
	GG(a, b, c, d, x[ 9], S21, 0x21e1cde6);
	GG(d, a, b, c, x[14], S22, 0xc33707d6);
	GG(c, d, a, b, x[ 3], S23, 0xf4d50d87);
	GG(b, c, d, a, x[ 8], S24, 0x455a14ed);
	GG(a, b, c, d, x[13], S21, 0xa9e3e905);
	GG(d, a, b, c, x[ 2], S22, 0xfcefa3f8);
	GG(c, d, a, b, x[ 7], S23, 0x676f02d9);
	GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);

	HH(a, b, c, d, x[ 5], S31, 0xfffa3942);
	HH(d, a, b, c, x[ 8], S32, 0x8771f681);
	HH(c, d, a, b, x[11], S33, 0x6d9d6122);
	HH(b, c, d, a, x[14], S34, 0xfde5380c);
	HH(a, b, c, d, x[ 1], S31, 0xa4beea44);
	HH(d, a, b, c, x[ 4], S32, 0x4bdecfa9);
	HH(c, d, a, b, x[ 7], S33, 0xf6bb4b60);
	HH(b, c, d, a, x[10], S34, 0xbebfbc70);
	HH(a, b, c, d, x[13], S31, 0x289b7ec6);
	HH(d, a, b, c, x[ 0], S32, 0xeaa127fa);
	HH(c, d, a, b, x[ 3], S33, 0xd4ef3085);
	HH(b, c, d, a, x[ 6], S34, 0x04881d05);
	HH(a, b, c, d, x[ 9], S31, 0xd9d4d039);
	HH(d, a, b, c, x[12], S32, 0xe6db99e5);
	HH(c, d, a, b, x[15], S33, 0x1fa27cf8);
	HH(b, c, d, a, x[ 2], S34, 0xc4ac5665);

	II(a, b, c, d, x[ 0], S41, 0xf4292244);
	II(d, a, b, c, x[ 7], S42, 0x432aff97);
	II(c, d, a, b, x[14], S43, 0xab9423a7);
	II(b, c, d, a, x[ 5], S44, 0xfc93a039);
	II(a, b, c, d, x[12], S41, 0x655b59c3);
	II(d, a, b, c, x[ 3], S42, 0x8f0ccc92);
	II(c, d, a, b, x[10], S43, 0xffeff47d);
	II(b, c, d, a, x[ 1], S44, 0x85845dd1);
	II(a, b, c, d, x[ 8], S41, 0x6fa87e4f);
	II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
	II(c, d, a, b, x[ 6], S43, 0xa3014314);
	II(b, c, d, a, x[13], S44, 0x4e0811a1);
	II(a, b, c, d, x[ 4], S41, 0xf7537e82);
	II(d, a, b, c, x[11], S42, 0xbd3af235);
	II(c, d, a, b, x[ 2], S43, 0x2ad7d2bb);
	II(b, c, d, a, x[ 9], S44, 0xeb86d391);

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

	memset((char*)x, 0, sizeof(x));
}

PRIVATE void md5_init(MD5_CTX *context)
{
	context->count[0] = context->count[1] = 0;
	context->state[0] = 0x67452301;
	context->state[1] = 0xefcdab89;
	context->state[2] = 0x98badcfe;
	context->state[3] = 0x10325476;
}

PRIVATE void md5_update(MD5_CTX *context, u8 *input, unsigned int inputLen)
{
	unsigned int i, index, partLen;

	index = (unsigned int)((context->count[0] >> 3) & 0x3F);

	if ((context->count[0] += ((u32)inputLen << 3)) < ((u32)inputLen << 3))
		context->count[1]++;
	context->count[1] += ((u32)inputLen >> 29);

	partLen = 64 - index;

	if (inputLen >= partLen) {
		memcpy((char*)&context->buffer[index], (char*)input, partLen);
		md5_transform(context->state, context->buffer);

		for (i = partLen; i + 63 < inputLen; i += 64)
			md5_transform(context->state, &input[i]);

		index = 0;
	} else {
		i = 0;
	}

	memcpy((char*)&context->buffer[index], (char*)&input[i], inputLen - i);
}

PRIVATE void md5_final(u8 digest[16], MD5_CTX *context)
{
	u8 bits[8];
	unsigned int index, padLen;

	md5_encode(bits, context->count, 8);

	index = (unsigned int)((context->count[0] >> 3) & 0x3f);
	padLen = (index < 56) ? (56 - index) : (120 - index);
	md5_update(context, PADDING, padLen);

	md5_update(context, bits, 8);

	md5_encode(digest, context->state, 16);

	memset((char*)context, 0, sizeof(*context));
}

PRIVATE char hex_chars[] = "0123456789abcdef";

PRIVATE void bytes_to_hex(u8 *bytes, int len, char *hex_str)
{
	int i;
	for (i = 0; i < len; i++) {
		hex_str[i * 2]     = hex_chars[(bytes[i] >> 4) & 0x0F];
		hex_str[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
	}
	hex_str[len * 2] = '\0';
}

/*****************************************************************************
 *                         calc_md5_for_file
 *****************************************************************************/
/**
 * FS 内部：计算 MD5(key || file_content || key)
 * 按扇区循环读取，避免一次性大内存分配
 *
 * @param pin   指向文件的 inode（必须是普通文件）
 * @param out   输出缓冲区，至少 MD5_STR_BUF_LEN (33) 字节
 * @return      0 成功，-1 失败
 */
PRIVATE int calc_md5_for_file(struct inode *pin, char out[MD5_STR_BUF_LEN])
{
	u8 digest[16];
	u8 key_bytes[4];
	MD5_CTX ctx;

	if (!pin || !out)
		return -1;

	/* 只对普通文件做校验，跳过设备文件/目录 */
	if (is_special(pin->i_mode))
		return -1;

	/* 确保 key 已初始化 */
	ensure_checksum_key_inited();

	/* key 转小端字节序 */
	key_bytes[0] = (u8)(s_ck_key & 0xFF);
	key_bytes[1] = (u8)((s_ck_key >> 8) & 0xFF);
	key_bytes[2] = (u8)((s_ck_key >> 16) & 0xFF);
	key_bytes[3] = (u8)((s_ck_key >> 24) & 0xFF);

	/* MD5(key || file || key) */
	md5_init(&ctx);
	md5_update(&ctx, key_bytes, 4);  /* 前置 key */

	/* 按扇区循环读取文件内容 */
	u32 bytes_left = pin->i_size;
	u32 sect_nr = pin->i_start_sect;

	while (bytes_left > 0) {
		/* 读一个扇区到 fsbuf */
		RD_SECT(pin->i_dev, sect_nr);

		/* 本次要处理的字节数 */
		u32 chunk = (bytes_left < SECTOR_SIZE) ? bytes_left : SECTOR_SIZE;

		/* 更新 MD5 */
		md5_update(&ctx, (u8*)fsbuf, chunk);

		bytes_left -= chunk;
		sect_nr++;
	}

	md5_update(&ctx, key_bytes, 4);  /* 后置 key */
	md5_final(digest, &ctx);

	/* 转为 32 字符 hex 字符串 */
	bytes_to_hex(digest, 16, out);

	return 0;
}

/*****************************************************************************
 *                                do_stat
 *****************************************************************************/
PUBLIC int do_stat()
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
	if (inode_nr == INVALID_INODE) {
		printl("{FS} FS::do_stat():: search_file() returns invalid inode: %s\n",
		       pathname);
		return -1;
	}

	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0)
		return -1;

	struct inode * pin = get_inode(dir_inode->i_dev, inode_nr);

	struct stat s;
	s.st_dev  = pin->i_dev;
	s.st_ino  = pin->i_num;
	s.st_mode = pin->i_mode;
	s.st_rdev = is_special(pin->i_mode) ? pin->i_start_sect : NO_DEV;
	s.st_size = pin->i_size;

	put_inode(pin);

	phys_copy((void*)va2la(src, fs_msg.BUF),
		  (void*)va2la(TASK_FS, &s),
		  sizeof(struct stat));

	return 0;
}

/****************************************************************************
 *                           do_get_checksum
 *****************************************************************************/
/* 只返回 inode 中存储的 md5_checksum（32字节） */
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

	phys_copy((void*)va2la(src, fs_msg.BUF),
		  (void*)va2la(TASK_FS, pin->md5_checksum),
		  MD5_HASH_LEN);

	put_inode(pin);
	return 0;
}

/****************************************************************************
 *                           do_calc_checksum
 *****************************************************************************/
/* FS 内部计算 MD5(key||file||key)，返回 32 字节 hex 字符串 */
PUBLIC int do_calc_checksum()
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

	char md5_str[MD5_STR_BUF_LEN];
	if (calc_md5_for_file(pin, md5_str) != 0) {
		put_inode(pin);
		return -1;
	}

	phys_copy((void*)va2la(src, fs_msg.BUF),
		  (void*)va2la(TASK_FS, md5_str),
		  MD5_HASH_LEN);

	put_inode(pin);
	return 0;
}

/****************************************************************************
 *                           do_verify_checksum
 *****************************************************************************/
/* FS 内部计算并与 inode 存储值比对，0=OK -1=FAIL */
PUBLIC int do_verify_checksum()
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

	char md5_str[MD5_STR_BUF_LEN];
	if (calc_md5_for_file(pin, md5_str) != 0)
	{
		put_inode(pin);
		return -1;
	}

	int i;
	for (i = 0; i < MD5_HASH_LEN; i++) {
		if (md5_str[i] != pin->md5_checksum[i]) {
			put_inode(pin);
			return -1;
		}
	}

	put_inode(pin);
	return 0;
}

/*****************************************************************************
 *                           do_refresh_checksums
 *****************************************************************************/
/**
 * 刷新根目录下所有可执行文件的校验和
 * 仅允许 INIT 进程调用，遍历根目录下所有普通文件，用当前 key 重新计算并写入 inode
 * 
 * @return  0 成功，-1 失败（非 INIT 调用）
 */
PUBLIC int do_refresh_checksums()
{
	int src = fs_msg.source;

	// 仅允许 INIT 进程调用，防止普通用户绕过校验
	if (src != INIT)
		return -1;

	int dev = root_inode->i_dev;
	int dir_blk0 = root_inode->i_start_sect;
	int nr_dir_blks = (root_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_entries = root_inode->i_size / DIR_ENTRY_SIZE;

	int seen = 0;
	int refreshed = 0;
	int i, j;
	char dir_sect_buf[SECTOR_SIZE];

	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dev, dir_blk0 + i);
		/*
		 * 注意：calc_md5_for_file() 内部会调用 RD_SECT 并覆写 fsbuf。
		 * 如果直接用 fsbuf 遍历目录项，pde 会在第一次算 hash 后失效。
		 * 所以这里先把目录扇区拷贝出来，再在本地缓冲区上遍历。
		 */
		memcpy(dir_sect_buf, (char*)fsbuf, SECTOR_SIZE);
		struct dir_entry * pde = (struct dir_entry *)dir_sect_buf;

		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE && seen < nr_entries; j++, pde++, seen++) {
			if (pde->inode_nr == 0)
				continue;

			char name[MAX_FILENAME_LEN + 1];
			memcpy(name, pde->name, MAX_FILENAME_LEN);
			name[MAX_FILENAME_LEN] = 0;

			/* 跳过特殊文件 */
			if (name[0] == '.')
				continue;
			if (strcmp(name, "cmd.tar") == 0)
				continue;
			if (strcmp(name, "kernel.bin") == 0 ||
			    strcmp(name, "hdboot.bin") == 0 ||
			    strcmp(name, "hdloader.bin") == 0)
				continue;
			/* /dev_tty0 /dev_tty1 /dev_tty2 ... */
			if (memcmp(name, "dev_tty", 7) == 0)
				continue;

			struct inode * pin = get_inode(dev, pde->inode_nr);
			int imode = pin->i_mode & I_TYPE_MASK;

			/* 只对普通文件做校验 */
			if (imode == I_REGULAR) {
				char md5_str[MD5_STR_BUF_LEN];
				if (calc_md5_for_file(pin, md5_str) == 0) {
					memcpy(pin->md5_checksum, md5_str, MD5_HASH_LEN);
					sync_inode(pin);
					refreshed++;
				}
			}
			put_inode(pin);
		}
	}

	return refreshed;
}

/*****************************************************************************
 *                                search_file
 *****************************************************************************/
PUBLIC int search_file(char * path)
{
	int i, j;

	char filename[MAX_PATH];
	memset(filename, 0, MAX_FILENAME_LEN);
	struct inode * dir_inode;
	if (strip_path(filename, path, &dir_inode) != 0)
		return 0;

	if (filename[0] == 0)
		return dir_inode->i_num;

	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_dir_entries = dir_inode->i_size / DIR_ENTRY_SIZE;

	int m = 0;
	struct dir_entry * pde;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			if (memcmp(filename, pde->name, MAX_FILENAME_LEN) == 0)
				return pde->inode_nr;
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries)
			break;
	}

	return 0;
}

/*****************************************************************************
 *                                strip_path
 *****************************************************************************/
PUBLIC int strip_path(char * filename, const char * pathname, struct inode** ppinode)
{
	const char * s = pathname;
	char * t = filename;

	if (s == 0)
		return -1;

	if (*s == '/')
		s++;

	while (*s) {
		if (*s == '/')
			return -1;
		*t++ = *s++;
		if (t - filename >= MAX_FILENAME_LEN)
			break;
	}
	*t = 0;

	*ppinode = root_inode;
	return 0;
}

