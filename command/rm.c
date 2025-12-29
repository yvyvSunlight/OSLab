#include "stdio.h"
#include "string.h"
#include "const.h"

static void normalize_path(const char *input, char *output);
static int remove_one(const char *arg, int force);

int main(int argc, char *argv[])
{
	int errors = 0;
	int force = 0;
	int targets = 0;
	int i;

	if (argc < 2) {
		printf("Usage: rm [-f] <file> [file...]\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-f") == 0) {
			force = 1;
			continue;
		}
		targets++;
		if (remove_one(argv[i], force) != 0)
			errors = 1;
	}

	if (targets == 0) {
		printf("Usage: rm [-f] <file> [file...]\n");
		return 1;
	}

	return errors;
}

static int remove_one(const char *arg, int force)
{
	char path[MAX_PATH];
	struct stat info;

	if (!arg || !*arg) {
		if (!force)
			printf("rm: invalid name\n");
		return -1;
	}

	normalize_path(arg, path);

	if (strcmp(path, "/") == 0) {
		printf("rm: refusing to remove root\n");
		return -1;
	}

	if (stat(path, &info) != 0) {
		if (!force)
			printf("rm: %s not found\n", path);
		return -1;
	}

	if ((info.st_mode & I_TYPE_MASK) == I_DIRECTORY) {
		printf("rm: %s is a directory (flat fs)\n", path);
		return -1;
	}

	if (unlink(path) != 0) {
		if (!force)
			printf("rm: cannot remove %s\n", path);
		return -1;
	}

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
