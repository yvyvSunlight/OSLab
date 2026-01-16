#include <stdio.h>
int i;
int* addr;
#define ENABLE_CANARY

unsigned char shellcode_dump[] = {
    0x55,         // push   ebp
    0x89, 0xe5,   // mov    ebp,esp
    0xeb, 0xfe    // eip = eip +2 -2  jump to self (infinite loop)
};

void ret2shellcode() {
    
// #ifdef ENABLE_CANARY
//     int canary = put_canary();
// #endif
    char buff[72] = {0};
    for (i = 0; i < 72; i++) {
        if (0 == shellcode_dump[i])
            break;
        buff[i] = shellcode_dump[i];
    }

    for (; i < 72; i++) {
        buff[i] = 0;
    }
    addr = &buff[72];
    for (i = 0; i < 3; i++) {
        addr[i] = buff;
    }

// #ifdef ENABLE_CANARY
//     canary_check(canary);
// #endif
}

void main(int argc, char* argv[]) {
    ret2shellcode();
}