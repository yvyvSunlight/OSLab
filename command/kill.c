#include "stdio.h"
#include "string.h"

static int parse_pid(const char *s, int *pid)
{
	int neg = 0;
	int value = 0;
	const char *p = s;

	if (!s || !*s)
		return 0;
	if (*p == '+')
		p++;
	else if (*p == '-')
		neg = 1, p++;

	if (!*p)
		return 0;
	while (*p) {
		if (*p < '0' || *p > '9')
			return 0;
		value = value * 10 + (*p - '0');
		p++;
	}

	*pid = neg ? -value : value;
	return 1;
}

int main(int argc, char *argv[])
{
	int i;
	int exit_code = 0;

	if (argc < 2) {
		printf("Usage: kill <pid> [pid...]\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		int pid;
		if (!parse_pid(argv[i], &pid)) {
			printf("kill: invalid pid '%s'\n", argv[i]);
			exit_code = 1;
			continue;
		}

		if (kill(pid) != 0) {
			printf("kill: failed to terminate %d\n", pid);
			exit_code = 1;
		}
	}

	return exit_code;
}
