#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/ringbuf"
#define READ_SIZE 128

int main(void)
{
	int fd;
	const char *msg = "test message from user space";
	char buf[READ_SIZE + 1];
	ssize_t written;
	ssize_t read_bytes;

	fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	written = write(fd, msg, strlen(msg));
	if (written < 0) {
		perror("write");
		close(fd);
		return 1;
	}

	read_bytes = read(fd, buf, READ_SIZE);
	if (read_bytes < 0) {
		perror("read");
		close(fd);
		return 1;
	}

	buf[read_bytes] = '\0';

	printf("write data: %s\n", msg);
	printf("write bytes: %zd\n", written);
	printf("read data: %s\n", buf);
	printf("read bytes: %zd\n", read_bytes);

	close(fd);
	return 0;
}
