#include "stdio.h"
int i;
int* addr;
static char msg[] = "Ret2Lib Attack Successful!\n";

int printf(const char *fmt, ...);
void exit(int status);
#define ENABLE_CANARY

void ret2lib() {
#ifdef ENABLE_CANARY
    int canary = put_canary();
#endif
    char buff[72] = {0};
    for (i = 0; i < 72; i++) {
        buff[i] = 0;
    }

    addr=&buff[72];

    addr[0] = 0xdeadbeef;  // ebp - 4
    addr[1] = 0xdeadbeef;  // ebp
    addr[2] = (int)printf; // return address
    addr[3] = (int)exit; // return address for printf
    addr[4] = (int)msg;  // argument for printf
    addr[5] = 0;        // argument for exit
#ifdef ENABLE_CANARY
    canary_check(canary);
#endif
}

void main(int argc, char* argv[]) {
    ret2lib();
}