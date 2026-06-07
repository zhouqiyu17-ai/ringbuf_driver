#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/ringbuf"

int main(void)
{
	int fd;
	const char *msg = "hello linux ring buffer";
	ssize_t ret;

	fd = open(DEVICE_PATH, O_WRONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	ret = write(fd, msg, strlen(msg));
	if (ret < 0) {
		perror("write");
		close(fd);
		return 1;
	}

	printf("write data: %s\n", msg);
	printf("write bytes: %zd\n", ret);

	close(fd);
	return 0;
}
