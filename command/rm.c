#include "stdio.h"
#include "string.h"

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

int main(int argc, char *argv[])
{
	int i;
	int errors = 0;

	if (argc < 2) {
		printf("Usage: rm <file> [file...]\n");
		return 1;
	}

	for (i = 1; i < argc; i++) {
		char normalized[MAX_PATH];
		normalize_path(argv[i], normalized);
		if (unlink(normalized) != 0) {
			printf("rm: cannot remove %s\n", normalized);
			errors = 1;
		}
	}

	return errors;
}
