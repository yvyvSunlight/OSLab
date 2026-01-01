#include "stdio.h"
int i;
int* addr;
#define ENABLE_CANARY
void backdoor() {
    printf("backdoor: Ret2Text Attack Successful!\n");
}

void ret2text() {
#ifdef ENABLE_CANARY
    int canary = put_canary();
#endif
    char buff[72] = {0};
    for (i = 0; i < 72; i++) {
        buff[i] = 0;
    }
    addr=&buff[72];
    for (i = 0; i < 6; i++) {
        addr[i] = 0x1000;
    }
#ifdef ENABLE_CANARY
    canary_check(canary);
#endif
}

void main(int argc, char* argv[]) {
    ret2text();
}