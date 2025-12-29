#include "stdio.h"
#include "string.h"
#include "const.h"

#define EDIT_BUF_SIZE 8192
#define ESC_KEY 0x1B

static char editor_buf[EDIT_BUF_SIZE];

static void normalize_path(const char *input, char *output);
static int has_binary_extension(const char *path);
static int is_probably_text(const char *path);
static int should_execute(const char *path, int existed);
static int run_executable(const char *path);
static int edit_text_file(const char *path, int existed);
static void print_content(const char *buf, int len);
static int capture_text(char *buf, int buf_size);
static int rewrite_file(const char *path, const char *buf, int len);

int main(int argc, char *argv[])
{
	char normalized[MAX_PATH];
	struct stat info;
	int existed;

	if (argc != 2) {
		printf("Usage: edit <path>\n");
		return 1;
	}

	normalize_path(argv[1], normalized);
	existed = (stat(normalized, &info) == 0);

	if (existed && (info.st_mode & I_TYPE_MASK) == I_DIRECTORY) {
		printf("edit: %s is a directory\n", normalized);
		return 1;
	}

	if (should_execute(normalized, existed))
		return run_executable(normalized);

	return edit_text_file(normalized, existed);
}

static int edit_text_file(const char *path, int existed)
{
	int fd;
	int total = 0;
	int n;
	int flags = existed ? O_RDWR : (O_CREAT | O_RDWR);

	fd = open(path, flags);
	if (fd < 0) {
		printf("edit: cannot open %s\n", path);
		return 1;
	}

	while (total < EDIT_BUF_SIZE - 1) {
		n = read(fd, editor_buf + total, EDIT_BUF_SIZE - 1 - total);
		if (n <= 0)
			break;
		total += n;
	}
	editor_buf[total] = 0;
	lseek(fd, 0, SEEK_SET);
	close(fd);

	if (total > 0)
		print_content(editor_buf, total);
	else
		printf("---- new file ----\n");

	printf("Enter new content. Press ESC to finish editing.\n");

	n = capture_text(editor_buf, EDIT_BUF_SIZE);
	if (n < 0)
		return 1;
	if (n == 0) {
		printf("edit: no changes written.\n");
		return 0;
	}

	if (rewrite_file(path, editor_buf, n) != 0)
		return 1;

	printf("edit: saved %d bytes to %s\n", n, path);
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

static int has_binary_extension(const char *path)
{
	const char *dot = 0;
	const char *p = path;

	while (*p) {
		if (*p == '/')
			dot = 0;
		else if (*p == '.')
			dot = p;
		p++;
	}

	if (!dot || !*(dot + 1))
		return 0;
	{
		char ext[8];
		int idx = 0;
		const char *src = dot + 1;
		while (*src && idx < (int)sizeof(ext) - 1) {
			char c = *src++;
			if (c >= 'A' && c <= 'Z')
				c = c - 'A' + 'a';
			ext[idx++] = c;
		}
		ext[idx] = 0;
		if (!strcmp(ext, "bin"))
			return 1;
		if (!strcmp(ext, "exe"))
			return 1;
		if (!strcmp(ext, "out"))
			return 1;
	}
	return 0;
}

static int is_probably_text(const char *path)
{
	char buf[256];
	int fd = open(path, O_RDWR);
	int i;
	int n;

	if (fd < 0)
		return 0;

	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n <= 0)
		return 0;

	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)buf[i];
		if (c == 0)
			return 0;
		if (c == '\n' || c == '\r' || c == '\t')
			continue;
		if (c < 0x20 || c > 0x7E)
			return 0;
	}
	return 1;
}

static int should_execute(const char *path, int existed)
{
	if (!existed)
		return 0;
	if (has_binary_extension(path))
		return 1;
	return !is_probably_text(path);
}

static int run_executable(const char *path)
{
	char *args[2];
	args[0] = (char*)path;
	args[1] = 0;
	if (execv(path, args) != 0) {
		printf("edit: cannot execute %s\n", path);
		return 1;
	}
	return 0;
}

static void print_content(const char *buf, int len)
{
	printf("---- current content (%d bytes) ----\n", len);
	write(1, buf, len);
	if (len == 0 || buf[len - 1] != '\n')
		printf("\n");
	printf("------------------------------------\n");
}

static int capture_text(char *buf, int buf_size)
{
	int total = 0;

	while (total < buf_size - 1) {
		char ch;
		int n = read(0, &ch, 1);
		if (n <= 0) {
			printf("edit: unexpected end of input.\n");
			return -1;
		}

		if ((unsigned char)ch == ESC_KEY)
			break;
		if (ch == '\r')
			continue;

		buf[total++] = ch;
	}

	if (total >= buf_size - 1)
		printf("edit: buffer full, truncating input.\n");

	buf[total] = 0;
	return total;
}

static int rewrite_file(const char *path, const char *buf, int len)
{
	int fd = open(path, O_RDWR | O_TRUNC);
	int written = 0;

	if (fd < 0)
		fd = open(path, O_CREAT | O_RDWR | O_TRUNC);
	if (fd < 0) {
		printf("edit: cannot write %s\n", path);
		return -1;
	}

	while (written < len) {
		int n = write(fd, buf + written, len - written);
		if (n <= 0)
			break;
		written += n;
	}
	close(fd);

	if (written != len) {
		printf("edit: short write\n");
		return -1;
	}
	return 0;
}
