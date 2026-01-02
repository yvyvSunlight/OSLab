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

/* 使用 RDTSC 获取 CPU 时钟周期数作为熵源 */
static inline u64 rdtsc(void) {
    u32 lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* 混合多个熵源的随机数生成器 */
static u32 entropy_pool = 0xdeadbeef;  // 非零初始值

static u32 mix_entropy(u32 a, u32 b) {
    // MurmurHash3 finalizer - 优秀的位混合
    a ^= b;
    a = (a ^ (a >> 16)) * 0x85ebca6b;
    a = (a ^ (a >> 13)) * 0xc2b2ae35;
    a = a ^ (a >> 16);
    return a;
}

/* 生成完整 32 位随机数 */
static u32 random32(void) {
    u64 tsc = rdtsc();
    
    // 混合时间戳的高位和低位
    entropy_pool = mix_entropy(entropy_pool, (u32)tsc);
    entropy_pool = mix_entropy(entropy_pool, (u32)(tsc >> 32));
    
    // 额外混合：使用栈地址作为熵源
    u32 stack_addr;
    asm volatile ("movl %%esp, %0" : "=r"(stack_addr));
    entropy_pool = mix_entropy(entropy_pool, stack_addr);
    
    // 再次混合 RDTSC（时间已变化）
    tsc = rdtsc();
    entropy_pool = mix_entropy(entropy_pool, (u32)tsc);
    
    return entropy_pool;
}

/* 
 * 生成带终止符的 canary（最低字节为 0x00）
 * 
 * 格式: 0xXXXXXX00
 *       ^^^^^^^^
 *       随机字节
 *               ^^
 *               固定为 0x00（空字节/字符串终止符）
 * 
 * 安全原理：
 * - strcpy 等函数遇到 0x00 会停止复制
 * - 攻击者必须覆盖这个 0x00 才能继续溢出
 * - 一旦覆盖，canary 值就会改变，触发检测
 */
static u32 random_terminator_canary(void) {
    u32 val = random32();
    
    // 确保高 3 字节非零（增加随机性）
    u8 *bytes = (u8 *)&val;
    for (int i = 1; i < 4; i++) {  // 跳过最低字节 bytes[0]
        if (bytes[i] == 0x00) {
            u32 replacement = (u32)rdtsc();
            bytes[i] = (replacement & 0xFF) | 0x01;
        }
    }
    // 强制最低字节为 0x00
    bytes[0] = 0x00; 
    // val &= 0xFFFFFF00;
    
    return val;
}

PUBLIC int put_canary() {
    int canary = (int)random_terminator_canary();
    
    asm volatile ("movl %0, %%gs:0x28" : : "r"(canary));
    return canary;
}

PUBLIC void canary_check(int value){
    int canary;
    asm volatile ("movl %%gs:0x28, %0" : "=r"(canary));
    if (value != canary) {
        printf("\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        printf("!                                                 !\n");
        printf("!    *** STACK SMASHING DETECTED ***              !\n");
        printf("!                                                 !\n");
        printf("!    Buffer overflow attack detected!             !\n");
        printf("!    Stack canary value has been corrupted.       !\n");
        printf("!                                                 !\n");
        printf("!    Expected: 0x%08x\n", (unsigned int)canary);
        printf("!    Found:    0x%08x\n", (unsigned int)value);
        printf("!                                                 !\n");
        printf("!    Process terminated for security.             !\n");
        printf("!                                                 !\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        printf("\n");
        exit(1); 
    }
}