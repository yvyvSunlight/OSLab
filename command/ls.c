#include "stdio.h"
#include "string.h"
#include "const.h"
#include "fs.h"

static void normalize_path(const char *input, char *output);
static void build_child_path(const char *dir, const char *name, char *out);
static void list_directory(const char *path);
static void list_file(const char *path, const char *label, const struct stat *info);
static void handle_target(const char *target, int show_header);
static char mode_to_char(int mode);
static void report_error(const char *target, const char *reason);

int main(int argc, char *argv[])
{
	int i;

	if (argc == 1) {
		handle_target("/", 0);
		return 0;
	}

	for (i = 1; i < argc; i++) {
		handle_target(argv[i], argc > 2);
		if (i != argc - 1)
			printf("\n");
	}

	return 0;
}

static void handle_target(const char *target, int show_header)
{
	char normalized[MAX_PATH];
	struct stat info;

	normalize_path(target, normalized);

	if (stat(normalized, &info) != 0) {
		report_error(target, "not found");
		return;
	}

	if ((info.st_mode & I_TYPE_MASK) == I_DIRECTORY) {
		if (show_header)
			printf("%s:\n", normalized);
		list_directory(normalized);
	} else {
		const char *label = (target && target[0]) ? target : normalized;
		list_file(normalized, label, &info);
	}
}

static void list_directory(const char *path)
{
	struct stat dir_info;
	int fd;
	int bytes_read = 0;
	int total = 0;
	struct dir_entry entry;

	if (stat(path, &dir_info) != 0) {
		report_error(path, "stat failed");
		return;
	}

	fd = open(path, O_RDWR);
	if (fd < 0) {
		report_error(path, "cannot open");
		return;
	}

	total = dir_info.st_size;
	while (bytes_read < total) {
		int n;
		memset(&entry, 0, sizeof(entry));
		n = read(fd, &entry, sizeof(entry));
		if (n != sizeof(entry))
			break;
		bytes_read += n;
		entry.name[MAX_FILENAME_LEN - 1] = 0;
		if (entry.inode_nr == 0)
			continue;
		if (entry.name[0] == 0)
			continue;
		if (entry.name[0] == '.' && entry.name[1] == 0)
			continue;

		{
			char child_path[MAX_PATH];
			struct stat child_info;

			build_child_path(path, entry.name, child_path);
			if (stat(child_path, &child_info) == 0)
				printf("%c %s\n", mode_to_char(child_info.st_mode), entry.name);
			else
				printf("? %s\n", entry.name);
		}
	}

	close(fd);
}

static void list_file(const char *path, const char *label, const struct stat *info)
{
	const char *name = (label && label[0]) ? label : path;
	printf("%c %s\n", mode_to_char(info->st_mode), name);
}

static void normalize_path(const char *input, char *output)
{
	const char *src;
	int i = 0;

	if (!input || !input[0] || (input[0] == '.' && input[1] == 0)) {
		output[0] = '/';
		output[1] = 0;
		return;
	}

	src = input;
	if (*src == '/') {
		output[i++] = '/';
		while (*src == '/')
			src++;
	} else {
		output[i++] = '/';
	}

	while (*src && i < MAX_PATH - 1)
		output[i++] = *src++;
	output[i] = 0;

	if (i > 1 && output[i - 1] == '/')
		output[i - 1] = 0;
}

static void build_child_path(const char *dir, const char *name, char *out)
{
	int len;

	if (!dir || dir[0] == 0 || (dir[0] == '/' && dir[1] == 0)) {
		sprintf(out, "/%s", name);
		return;
	}

	len = strlen(dir);
	if (dir[len - 1] == '/')
		sprintf(out, "%s%s", dir, name);
	else
		sprintf(out, "%s/%s", dir, name);
}

static char mode_to_char(int mode)
{
	switch (mode & I_TYPE_MASK) {
	case I_DIRECTORY:
		return 'd';
	case I_CHAR_SPECIAL:
		return 'c';
	case I_BLOCK_SPECIAL:
		return 'b';
	case I_NAMED_PIPE:
		return 'p';
	default:
		return '-';
	}
}

static void report_error(const char *target, const char *reason)
{
	if (target && target[0])
		printf("ls: %s: %s\n", target, reason);
	else
		printf("ls: %s\n", reason);
}
