/*************************************************************************//**
 *****************************************************************************
 * @file   kernel/filecheck.c
 * @brief  文件完整性校验 - MD5实现
 * @date   2026
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
#include "proto.h"

/*===========================================================================*
 *                              MD5 实现                                     *
 *===========================================================================*/

/* MD5 context 结构体 */
typedef struct {
    u32 state[4];      /* state (ABCD) */
    u32 count[2];      /* number of bits, modulo 2^64 (lsb first) */
    u8  buffer[64];    /* input buffer */
} MD5_CTX;

/* MD5 常量定义 */
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

/* F, G, H, I 为基本的 MD5 函数 */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT 将 x 左移 n 位 */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* FF, GG, HH, II 为四轮变换 */
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

/* MD5 padding 数据 */
PRIVATE u8 PADDING[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*****************************************************************************
 *                                md5_encode
 *****************************************************************************/
/**
 * 将 u32 数组编码为 u8 数组
 */
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

/*****************************************************************************
 *                                md5_decode
 *****************************************************************************/
/**
 * 将 u8 数组解码为 u32 数组
 */
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

/*****************************************************************************
 *                                md5_transform
 *****************************************************************************/
/**
 * MD5 基本变换，变换 state 基于 block
 */
PRIVATE void md5_transform(u32 state[4], u8 block[64])
{
    u32 a = state[0], b = state[1], c = state[2], d = state[3], x[16];

    md5_decode(x, block, 64);

    /* Round 1 */
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

    /* Round 2 */
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

    /* Round 3 */
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

    /* Round 4 */
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

    /* 清除敏感信息 */
    memset((char *)x, 0, sizeof(x));
}

/*****************************************************************************
 *                                md5_init
 *****************************************************************************/
/**
 * 初始化 MD5 上下文
 */
PRIVATE void md5_init(MD5_CTX *context)
{
    context->count[0] = context->count[1] = 0;
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
}

/*****************************************************************************
 *                                md5_update
 *****************************************************************************/
/**
 * 更新 MD5 上下文，处理输入数据
 */
PRIVATE void md5_update(MD5_CTX *context, u8 *input, unsigned int inputLen)
{
    unsigned int i, index, partLen;

    /* 计算已有数据的字节数 mod 64 */
    index = (unsigned int)((context->count[0] >> 3) & 0x3F);

    /* 更新位计数 */
    if ((context->count[0] += ((u32)inputLen << 3)) < ((u32)inputLen << 3))
        context->count[1]++;
    context->count[1] += ((u32)inputLen >> 29);

    partLen = 64 - index;

    /* 尽可能多地变换 */
    if (inputLen >= partLen) {
        memcpy((char *)&context->buffer[index], (char *)input, partLen);
        md5_transform(context->state, context->buffer);

        for (i = partLen; i + 63 < inputLen; i += 64)
            md5_transform(context->state, &input[i]);

        index = 0;
    } else {
        i = 0;
    }

    /* 缓存剩余输入 */
    memcpy((char *)&context->buffer[index], (char *)&input[i], inputLen - i);
}

/*****************************************************************************
 *                                md5_final
 *****************************************************************************/
/**
 * 完成 MD5 计算，输出摘要
 */
PRIVATE void md5_final(u8 digest[16], MD5_CTX *context)
{
    u8 bits[8];
    unsigned int index, padLen;

    /* 保存位计数 */
    md5_encode(bits, context->count, 8);

    /* 填充到 56 mod 64 */
    index = (unsigned int)((context->count[0] >> 3) & 0x3f);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    md5_update(context, PADDING, padLen);

    /* 添加长度 */
    md5_update(context, bits, 8);

    /* 输出结果 */
    md5_encode(digest, context->state, 16);

    /* 清除敏感信息 */
    memset((char *)context, 0, sizeof(*context));
}

/*===========================================================================*
 *                          文件校验接口                                      *
 *===========================================================================*/

PRIVATE char hex_chars[] = "0123456789abcdef";

/*****************************************************************************
 *                                bytes_to_hex
 *****************************************************************************/
/**
 * 将字节数组转换为十六进制字符串
 */
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
 *                       compute_md5_with_key_fd
 *****************************************************************************/
/**
 * 对文件描述符执行 MD5(key || file || key) 计算，按实际文件长度分块读取。
 *
 * @param fd        已打开文件描述符，调用前后会rewind到开头
 * @param data_len  文件总长度（字节）
 * @param key       32位key
 * @param result    输出33字节缓冲区（32字符 + '\0'）
 * @return          0 成功，-1 失败
 */
PUBLIC int compute_md5_with_key_fd(int fd, u32 data_len, u32 key, char *result)
{
    u8 digest[16];
    u8 key_bytes[4];
    u8 buf[SECTOR_SIZE * 4]; /* 2KB 缓冲，避免大栈占用 */
    MD5_CTX ctx;

    if (lseek(fd, 0, SEEK_SET) < 0)
        return -1;

    key_bytes[0] = (u8)(key & 0xFF);
    key_bytes[1] = (u8)((key >> 8) & 0xFF);
    key_bytes[2] = (u8)((key >> 16) & 0xFF);
    key_bytes[3] = (u8)((key >> 24) & 0xFF);

    md5_init(&ctx);
    md5_update(&ctx, key_bytes, 4); /* 前置 key */

    u32 left = data_len;
    while (left > 0) {
        u32 chunk = left < sizeof(buf) ? left : (u32)sizeof(buf);
        int r = read(fd, buf, chunk);
        if (r != (int)chunk)
            return -1;
        md5_update(&ctx, buf, chunk);
        left -= chunk;
    }

    md5_update(&ctx, key_bytes, 4); /* 后置 key */
    md5_final(digest, &ctx);
    bytes_to_hex(digest, 16, result);

    /* rewind 方便后续调用者复用 fd */
    lseek(fd, 0, SEEK_SET);
    return 0;
}

/*****************************************************************************
 *                                compare_md5_strings
 *****************************************************************************/
/**
 * 比较两个MD5字符串
 * 
 * @param md5_1    第一个MD5字符串
 * @param md5_2    第二个MD5字符串
 * @return         0 如果相等，非0 如果不等
 */
PUBLIC int compare_md5_strings(const char *md5_1, const char *md5_2)
{
    int i;
    for (i = 0; i < 32; i++) {
        if (md5_1[i] != md5_2[i])
            return 1;
    }
    return 0;
}
