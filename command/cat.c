#include "stdio.h"

int main(int argc, char *argv[])
{
	int fd;
	char buf[1024];
	int n;

	if (argc != 2) {
		printf("Usage: cat <file>\n");
		return 1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		printf("cat: %s: No such file or directory\n", argv[1]);
		return 1;
	}

	while ((n = read(fd, buf, 1023)) > 0) {
		buf[n] = '\0';
		printf("%s", buf);
	}

	printf("\n");
	close(fd);
	return 0;
}

