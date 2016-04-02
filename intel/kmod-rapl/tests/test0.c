
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint64_t UInt64;

int main(int argc, char **argv)
{
	int fd;
	char path[64];
	int sz;
	UInt64 buff;

	if (2 != argc) {
		fprintf(stderr, "usage: %s <cpu>\n", argv[0]);
		return 1;
	}

	snprintf(path, sizeof(path), "/dev/cpu/%d/rapl", strtol(argv[1], NULL, 10));

	fd = open(path, O_RDONLY);
	fprintf(stderr, "fd = %d\n", fd);

	sz = pread(fd, &buff, sizeof(buff), 0x606);
	fprintf(stderr, "sz = %d\n", sz);
	sz = pread(fd, &buff, sizeof(buff), 0x611);
	fprintf(stderr, "sz = %d\n", sz);
	sz = pread(fd, &buff, sizeof(buff), 0x619);
	fprintf(stderr, "sz = %d\n", sz);
	sz = pread(fd, &buff, sizeof(buff), 0x639);
	fprintf(stderr, "sz = %d\n", sz);
	sz = pread(fd, &buff, sizeof(buff), 0x641);
	fprintf(stderr, "sz = %d\n", sz);

	return 0;
}

