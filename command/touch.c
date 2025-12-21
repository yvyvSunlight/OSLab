#include "stdio.h"
#include "string.h"
#include "const.h"

static void normalize_path(const char *input, char *output)
{
	const char *src = input;
	int i = 0;

	if (!input || !*input) {
		output[0] = '/';
		output[1] = 0;
		return;
	}

	if (*src != '/') {
		output[i++] = '/';
	}

	while (*src && i < MAX_PATH - 1)
		output[i++] = *src++;
	output[i] = 0;

	if (i > 1 && output[i - 1] == '/')
		output[i - 1] = 0;
}

static int create_file(const char *path)
{
	char normalized[MAX_PATH];
	int fd;

	normalize_path(path, normalized);
	fd = open(normalized, O_CREAT | O_RDWR | O_TRUNC);
	if (fd < 0) {
		printf("touch: cannot create %s\n", normalized);
		return -1;
	}
	{
		const char *placeholder = "\n";
		write(fd, placeholder, 1);
		lseek(fd, 0, SEEK_SET);
	}
	close(fd);
	return 0;
}

int main(int argc, char *argv[])
{
	int i;
	int errors = 0;

	if (argc < 2) {
		printf("Usage: touch <file> [file...]\n");
		return 1;
	}

	for (i = 1; i < argc; i++)
		if (create_file(argv[i]) != 0)
			errors = 1;

	return errors;
}
