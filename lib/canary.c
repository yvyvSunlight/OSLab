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

// 复制 random 函数的依赖变量和实现，或者简化实现
static unsigned long long next = 1;
int random(void) {
    next = next * 1103515245 + 12345;
    return (unsigned int)(next/65536) % 32768;
}

PUBLIC int put_canary() {
    int canary = random();
    // 注意：用户态程序是否有权限访问 GS:0x28？
    // 如果 GS 指向的是用户态 TCB，则没问题。
    asm("movl %0, %%gs:0x28" : "=r"(canary));
    return canary;
}

PUBLIC void canary_check(int value){
    int canary;
    asm("movl %%gs:0x28, %0" : "=r"(canary));
    if (value != canary) {
        printf("stack smashing detected!!\n");
        // 用户态不能 panic，应该退出
        exit(1); 
    }
}
