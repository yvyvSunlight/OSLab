#include "stdio.h"
#include "string.h"
#include "const.h"

static void normalize_path(const char *input, char *output);
static int create_file(const char *user_path);

int main(int argc, char *argv[])
{
	int i;
	int rc = 0;

	if (argc < 2) {
		printf("Usage: touch <name> [name ...]\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (create_file(argv[i]) != 0)
			rc = 1;
	}

	return rc;
}

static int create_file(const char *user_path)
{
	char path[MAX_PATH];
	int fd;

	if (!user_path || !*user_path) {
		printf("touch: invalid name\n");
		return -1;
	}

	normalize_path(user_path, path);

	fd = open(path, O_CREAT | O_RDWR | O_TRUNC);
	if (fd < 0) {
		printf("touch: cannot create %s\n", path);
		return -1;
	}
	close(fd);
	printf("touch: created %s\n", path);
	return 0;
}

static void normalize_path(const char *input, char *output)
{
	const char *src = input;
	int i = 0;

	if (!input || !*input) {
		output[0] = '/';
		output[1] = 0;
		return;
	}

	if (*src != '/')
		output[i++] = '/';

	while (*src && i < MAX_PATH - 1)
		output[i++] = *src++;
	output[i] = 0;

	if (i > 1 && output[i - 1] == '/')
		output[i - 1] = 0;
}
