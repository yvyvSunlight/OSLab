#include "stdio.h"
#include "const.h"
#include "type.h"
#include "string.h"

#define BUFFER_SIZE 1024
#define MAX_LINES 1000
#define MAX_COLUMNS 80
#define MAX_OFFSET (MAX_LINES * MAX_COLUMNS)

static int current_line = 0;
static int current_column = 0;
static long cursor_offset = 0;
static long current_file_size = 0;
static const char *active_filename = NULL;

static int is_line_break(char ch)
{
	return ch == '\n' || ch == '\r';
}

static void trim_line_breaks(char *str)
{
	int len = strlen(str);
	while (len > 0 && is_line_break(str[len - 1])) {
		str[len - 1] = '\0';
		len--;
	}
}

static int string_to_int(const char *str)
{
	int i = 0;
	int result = 0;

	while (str[i] == ' ' || str[i] == '\t')
		i++;

	if (str[i] == '\0' || is_line_break(str[i]))
		return -1;

	while (str[i] != '\0' && !is_line_break(str[i])) {
		if (str[i] < '0' || str[i] > '9') {
			printf("Invalid number:%s\n", str);
			return -1;
		}
		result = result * 10 + (str[i] - '0');
		i++;
	}

	return result;
}

static int open_file(const char *filename, int flags);
static void write_content(const char *filename);
static void delete_content(const char *filename);
static void show_content(const char *filename);
static void set_position(void);
static void clear_input_buffer(void);
static void display_current_position(void);
static void move_cursor(char direction);
static void fill_file_to_position(int fd, int position);
static void update_cursor_display(void);
static long refresh_file_size(const char *filename);

int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("Usage: edit <file>\n");
		return 0;
	}

	char *filename = argv[1];
	active_filename = filename;
	refresh_file_size(filename);
	update_cursor_display();
	char operation;

	while (1) {
		display_current_position();
		printf("\nChoose your operation:\n");
		printf("1. Write\n");
		printf("2. Delete\n");
		printf("3. Show\n");
		printf("4. Choose Position\n");
		printf("5. Exit\n");
		printf("Enter your choice (or use w/a/s/d to move cursor): ");

		if (read(0, &operation, 1) != 1) {
			printf("Failed to read operation.\n");
			continue;
		}
		clear_input_buffer();

		switch (operation) {
		case '1':
			write_content(filename);
			break;
		case '2':
			delete_content(filename);
			break;
		case '3':
			show_content(filename);
			break;
		case '4':
			set_position();
			break;
		case '5':
			printf("Exiting editor.\n");
			return 0;
		case 'w':
		case 'a':
		case 's':
		case 'd':
			move_cursor(operation);
			break;
		default:
			printf("Invalid operation.\n");
			break;
		}
	}

	return 0;
}

static int open_file(const char *filename, int flags)
{
	int fd = open(filename, flags);
	if (fd == -1)
		printf("edit: %s: No such file or directory\n", filename);
	return fd;
}

static void clear_input_buffer(void)
{
	char ch;
	while (read(0, &ch, 1) == 1 && !is_line_break(ch))
		;
}

static void write_content(const char *filename)
{
	printf("Input your content:\n");
	char content[BUFFER_SIZE];
	int bytes_read = read(0, content, BUFFER_SIZE - 1);
	if (bytes_read < 0) {
		printf("Failed to read content.\n");
		return;
	}
	content[bytes_read] = '\0';
	int content_len = bytes_read;
	int i;
	for (i = 0; i < content_len; i++) {
		if (content[i] == '\r')
			content[i] = '\n';
	}
	if (content_len == 0) {
		printf("No content to write.\n");
		return;
	}

	int fd = open_file(filename, O_RDWR);
	if (fd == -1)
		return;

	int position = (int)cursor_offset;
	if (position > MAX_OFFSET)
		position = MAX_OFFSET;
	fill_file_to_position(fd, position);

	if (lseek(fd, position, SEEK_SET) == -1) {
		printf("Failed to seek to position.\n");
		close(fd);
		return;
	}

	int bytes_written = write(fd, content, content_len);
	if (bytes_written == -1) {
		printf("Failed to write to file.\n");
	} else {
		printf("%d bytes written.\n", bytes_written);
		cursor_offset += bytes_written;
		if (cursor_offset > MAX_OFFSET)
			cursor_offset = MAX_OFFSET;
		int new_size = lseek(fd, 0, SEEK_END);
		if (new_size >= 0)
			current_file_size = new_size;
		else if (cursor_offset > current_file_size)
			current_file_size = cursor_offset;
		update_cursor_display();
	}

	close(fd);
}

static void delete_content(const char *filename)
{
	printf("Input how many bytes you want to delete:\n");
	char nr_char[10] = {0};

	if (read(0, nr_char, sizeof(nr_char) - 1) < 0) {
		printf("Failed to read delete count.\n");
		return;
	}
	nr_char[sizeof(nr_char) - 1] = '\0';
	trim_line_breaks(nr_char);

	int delete_count = string_to_int(nr_char);
	if (delete_count <= 0) {
		printf("Invalid delete count.\n");
		return;
	}

	int fd = open_file(filename, O_RDWR);
	if (fd == -1)
		return;

	int file_size = lseek(fd, 0, SEEK_END);
	if (file_size < 0) {
		printf("Failed to determine file size.\n");
		close(fd);
		return;
	}
	current_file_size = file_size;
	if (cursor_offset > current_file_size)
		cursor_offset = current_file_size;
	if (delete_count > cursor_offset)
		delete_count = cursor_offset;
	if (delete_count == 0) {
		printf("Nothing to delete.\n");
		close(fd);
		return;
	}

	char buffer[BUFFER_SIZE];
	int read_pos = (int)cursor_offset;
	while (1) {
		if (lseek(fd, read_pos, SEEK_SET) == -1) {
			printf("Failed to seek while deleting.\n");
			close(fd);
			return;
		}
		int bytes_read = read(fd, buffer, BUFFER_SIZE);
		if (bytes_read <= 0)
			break;
		if (lseek(fd, read_pos - delete_count, SEEK_SET) == -1) {
			printf("Failed to reposition during delete.\n");
			close(fd);
			return;
		}
		if (write(fd, buffer, bytes_read) != bytes_read) {
			printf("Failed to shift file content.\n");
			close(fd);
			return;
		}
		read_pos += bytes_read;
	}

	if (ftruncate(fd, current_file_size - delete_count) == -1) {
		printf("Failed to truncate file.\n");
		close(fd);
		return;
	}

	current_file_size -= delete_count;
	cursor_offset -= delete_count;
	if (cursor_offset < 0)
		cursor_offset = 0;
	update_cursor_display();
	printf("Deleted %d bytes.\n", delete_count);

	close(fd);
}

static void show_content(const char *filename)
{
	int fd = open_file(filename, O_RDWR);
	if (fd == -1)
		return;

	char buffer[BUFFER_SIZE];
	int file_size = lseek(fd, 0, SEEK_END);
	if (file_size < 0) {
		printf("Failed to get file size.\n");
		close(fd);
		return;
	}
	current_file_size = file_size;
	if (lseek(fd, 0, SEEK_SET) == -1) {
		printf("Failed to seek.\n");
		close(fd);
		return;
	}

	int bytes_read;
	while ((bytes_read = read(fd, buffer, BUFFER_SIZE - 1)) > 0) {
		buffer[bytes_read] = '\0';
		printf("%s", buffer);
	}

	printf("\n");

	if (bytes_read == -1)
		printf("Failed to read from file.\n");

	if (cursor_offset > current_file_size) {
		cursor_offset = current_file_size;
		update_cursor_display();
	}

	close(fd);
}

static void set_position(void)
{
	printf("Input your line and column (format: line,column):\n");
	char pos_str[20];
	if (read(0, pos_str, sizeof(pos_str) - 1) < 0) {
		printf("Failed to read position.\n");
		return;
	}
	pos_str[sizeof(pos_str) - 1] = '\0';
	trim_line_breaks(pos_str);

	char *comma = strchr(pos_str, ',');
	if (comma == NULL) {
		printf("Invalid format. Use line,column.\n");
		return;
	}

	*comma = '\0';
	int new_line = string_to_int(pos_str);
	int new_column = string_to_int(comma + 1);

	if (new_line < 0 || new_column < 0 || new_column >= MAX_COLUMNS) {
		printf("Invalid position.\n");
		return;
	}

	long new_offset = (long)new_line * MAX_COLUMNS + new_column;
	if (new_offset > MAX_OFFSET)
		new_offset = MAX_OFFSET;
	cursor_offset = new_offset;
	update_cursor_display();
	printf("Current position set to line %d, column %d.\n", current_line, current_column);
}

static void display_current_position(void)
{
	update_cursor_display();
	printf("Current position: line %d, column %d\n", current_line, current_column);
}

static void move_cursor(char direction)
{
	switch (direction) {
	case 'a':
		if (cursor_offset > 0)
			cursor_offset--;
		break;
	case 's':
		cursor_offset += MAX_COLUMNS;
		if (cursor_offset > current_file_size)
			cursor_offset = current_file_size;
		break;
	case 'w':
		cursor_offset -= MAX_COLUMNS;
		if (cursor_offset < 0)
			cursor_offset = 0;
		break;
	case 'd':
		if (cursor_offset < current_file_size)
			cursor_offset++;
		break;
	default:
		break;
	}
	update_cursor_display();
	display_current_position();
}

static void fill_file_to_position(int fd, int position)
{
	int file_size = lseek(fd, 0, SEEK_END);
	if (file_size == -1) {
		printf("Failed to get file size.\n");
		return;
	}

	if (position > file_size) {
		char spaces[BUFFER_SIZE];
		memset(spaces, ' ', BUFFER_SIZE);

		while (file_size < position) {
			int chunk_size = (position - file_size) < BUFFER_SIZE ? (position - file_size) : BUFFER_SIZE;
			if (write(fd, spaces, chunk_size) == -1) {
				printf("Failed to write spaces to file.\n");
				return;
			}
			file_size += chunk_size;
		}
	}
}

static void update_cursor_display(void)
{
	if (cursor_offset < 0)
		cursor_offset = 0;
	if (cursor_offset > MAX_OFFSET)
		cursor_offset = MAX_OFFSET;
	current_line = cursor_offset / MAX_COLUMNS;
	current_column = cursor_offset % MAX_COLUMNS;
}

static long refresh_file_size(const char *filename)
{
	if (!filename)
		return current_file_size;
	int fd = open(filename, O_RDWR);
	if (fd == -1) {
		current_file_size = 0;
		cursor_offset = 0;
		update_cursor_display();
		return current_file_size;
	}
	int size = lseek(fd, 0, SEEK_END);
	if (size < 0)
		size = 0;
	close(fd);
	current_file_size = size;
	if (cursor_offset > current_file_size)
		cursor_offset = current_file_size;
	update_cursor_display();
	return current_file_size;
}


