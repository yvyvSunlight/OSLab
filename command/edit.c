#include "stdio.h"
#include "string.h"
#include "const.h"

#define EDIT_BUF_SIZE 8192

static void normalize_path(const char *input, char *output);
static int is_text_file(const char *path);
static int capture_text(char *buf, int buf_size);
static int rewrite_file(const char *path, const char *buf, int len);
static int read_line(char *buf, int buf_size);
static int edit_text(const char *path, int existed);

int main(int argc, char *argv[])
{
	char normalized[MAX_PATH];
	struct stat info;
	int need_stat;

	if (argc != 2) {
		printf("Usage: edit <path>\n");
		return 1;
	}

	normalize_path(argv[1], normalized);
	need_stat = (stat(normalized, &info) == 0);

	if (need_stat && (info.st_mode & I_TYPE_MASK) == I_DIRECTORY) {
		printf("edit: %s is a directory\n", normalized);
		return 1;
	}

	if (need_stat && !is_text_file(normalized)) {
		char *args[2];
		args[0] = normalized;
		args[1] = 0;
		if (execv(normalized, args) != 0) {
			printf("edit: cannot execute %s\n", normalized);
			return 1;
		}
		return 0;
	}

	return edit_text(normalized, need_stat);
}

static int edit_text(const char *path, int existed)
{
	char buffer[EDIT_BUF_SIZE];
	int fd;
	int total = 0;
	int n;
	int flags = existed ? O_RDWR : (O_CREAT | O_RDWR);

	fd = open(path, flags);
	if (fd < 0 && existed)
		fd = open(path, O_CREAT | O_RDWR);
	if (fd < 0) {
		printf("edit: cannot open %s\n", path);
		return 1;
	}

	while (total < EDIT_BUF_SIZE - 1) {
		n = read(fd, buffer + total, EDIT_BUF_SIZE - 1 - total);
		if (n <= 0)
			break;
		total += n;
	}
	buffer[total] = 0;
	lseek(fd, 0, SEEK_SET);
	close(fd);

	if (total > 0) {
		printf("---- current content (%d bytes) ----\n", total);
		printf("%s\n", buffer);
	}
	else {
		printf("---- new file ----\n");
	}

	printf("Enter new content. Use a single '.' on a line to finish.\n");
	total = capture_text(buffer, EDIT_BUF_SIZE);
	if (total < 0)
		return 1;
	if (total == 0) {
		printf("edit: no changes written.\n");
		return 0;
	}

	if (rewrite_file(path, buffer, total) != 0)
		return 1;

	printf("edit: saved %d bytes to %s\n", total, path);
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

static int is_text_file(const char *path)
{
	char buf[256];
	int fd = open(path, O_RDWR);
	int i;
	int n;

	if (fd < 0)
		return 1;

	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n <= 0)
		return 1;

	for (i = 0; i < n; i++) {
		unsigned char c = buf[i];
		if (c == '\n' || c == '\r' || c == '\t')
			continue;
		if (c < 0x20 || c > 0x7E)
			return 0;
	}
	return 1;
}

static int capture_text(char *buf, int buf_size)
{
	int total = 0;

	while (1) {
		char line[256];
		int len;

		printf("> ");
		len = read_line(line, sizeof(line));
		if (len < 0) {
			printf("edit: unexpected end of input.\n");
			return -1;
		}

		if (len == 2 && line[0] == '.' && line[1] == '\n')
			break;

		if (total + len >= buf_size - 1) {
			printf("edit: buffer full, truncating input.\n");
			len = buf_size - 1 - total;
		}

		memcpy(buf + total, line, len);
		total += len;
		if (total >= buf_size - 1)
			break;
	}

	buf[total] = 0;
	return total;
}

static int read_line(char *buf, int buf_size)
{
	int pos = 0;

	while (pos < buf_size - 1) {
		char ch;
		int n = read(0, &ch, 1);
		if (n <= 0) {
			if (pos == 0)
				return -1;
			break;
		}
		if (ch == '\r')
			continue;
		buf[pos++] = ch;
		if (ch == '\n')
			break;
	}

	buf[pos] = 0;
	return pos;
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
