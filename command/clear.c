#include "stdio.h"

int main(void)
{
	if (clear_screen_cmd() != 0) {
		printf("clear: failed\n");
		return 1;
	}
	return 0;
}
