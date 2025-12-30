#include "stdio.h"
int i;
int* addr;

void backdoor() {
    printf("backdoor: Ret2Text Attack Successful!\n");
}

void ret2text() {
    char buff[72] = {0};
    for (i = 0; i < 72; i++) {
        buff[i] = 0;
    }
    addr=&buff[72];
    for (i = 0; i < 4; i++) {
        addr[i] = 0x1000;
    }
}

void main(int argc, char* argv[]) {
    ret2text();
}