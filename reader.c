#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/ringbuf"
#define READ_SIZE 128

int main(void)
{
	int fd;
	char buf[READ_SIZE + 1];
	ssize_t ret;

	fd = open(DEVICE_PATH, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	ret = read(fd, buf, READ_SIZE);
	if (ret < 0) {
		perror("read");
		close(fd);
		return 1;
	}

	buf[ret] = '\0';

	printf("read data: %s\n", buf);
	printf("read bytes: %zd\n", ret);

	close(fd);
	return 0;
}
