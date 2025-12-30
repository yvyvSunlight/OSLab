#include <stdio.h>

void main() {
    unsigned int sp;
    asm("movl %%esp, %0" : "=r"(sp));
    printf("fmt: 0x%x\n", sp);
    unsigned int tmp = 0;
 
    for (unsigned int i = 0; i < 0x4000 * 2; i++) {
        asm("push %0" : "=r"(tmp));
    }
    
    asm("movl %%esp, %0" : "=r"(sp));
    printf("fmt: 0x%x\n", sp);
    return 0;
}
