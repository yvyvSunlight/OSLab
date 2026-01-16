#include "sys/const.h"
#include "stdio.h"
#include "string.h"
#include "type.h"
// #include "syscall.h"
// #include "log.h"
#include "elf.h"


#define PAGESIZE 4096

/* 函数声明 */
void inject(char *elf_file);
void insert(Elf32_Ehdr elf_ehdr, char *elf_file, int old_entry);

/* 打印帮助信息 */
void help()
{
    printf("Usage: inject_only <file>\n");
}

/* 主函数：解析命令行参数并调用相应的 POC */
int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        help();
        return 0;
    }
    inject(argv[1]); // 直接执行 ELF 注入
    return 0;
}

/* 注入逻辑的主要入口 */
void inject(char *elf_file)
{
    printf("start to inject...\n");

    int old_entry;
    Elf32_Ehdr elf_ehdr;
    Elf32_Phdr elf_phdr;

    // 1. 打开 ELF文件并读取 ELF 头 拿到entry地址
    int old_file = open(elf_file, O_RDWR);
    read(old_file, &elf_ehdr, sizeof(Elf32_Ehdr));
    old_entry = elf_ehdr.e_entry; // 保存原始入口地址
    printf("old_entry: 0x%x\n", old_entry);

    close(old_file);

    printf("Inserting the injector...\n");
    // 在程序入口 插入注入程序
    insert(elf_ehdr, elf_file, old_entry);
}

/* 执行代码插入和文件修改 */
void insert(Elf32_Ehdr elf_ehdr, char *elf_file, int old_entry)
{
    // 将原始入口地址转换为字节数组，以便嵌入到 shellcode 中
    int old_entry_addr[4];

    char inject_code[] = {
        0x55,       // push   ebp
        0x89, 0xe5, // mov    ebp,esp
        0xeb, 0xfe  // eip = eip +2 -2  jump to self (infinite loop)
    };
    int inject_size = sizeof(inject_code);

    // 防止注入代码太大
    if (inject_size > PAGESIZE)
    {
        printf("Injecting code is too big!\n");
        exit(0);
    }

    //  4. 写入注入代码
    int old_file = open(elf_file, O_RDWR);
    u8 buffer[20000];
    read(old_file, buffer, old_entry);         // 跳过前 old_entry 字节
    write(old_file, inject_code, inject_size); // 写入 shellcode
    close(old_file);

    // 5. 验证写入结果 (可选)
    old_file = open(elf_file, O_RDWR);
    read(old_file, buffer, old_entry);
    read(old_file, buffer, 5);
    printf("After injecting code:\n");
    for (int i = 0; i < 5; i++)
    {
        printf("0x%x ", buffer[i]);
    }
    printf("\n");
    close(old_file);
    printf("Finished!\n");
}
