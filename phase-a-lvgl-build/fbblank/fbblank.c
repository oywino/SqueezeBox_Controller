#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if(argc != 2) {
    fprintf(stderr, "usage: %s <blank_mode>\n", argv[0]);
    fprintf(stderr, "  0=UNBLANK 1=NORMAL 2=VSYNC_SUSPEND 3=HSYNC_SUSPEND 4=POWERDOWN\n");
    return 2;
  }

  int mode = atoi(argv[1]);
  int fd = open("/dev/fb0", O_RDWR);
  if(fd < 0) {
    fprintf(stderr, "open /dev/fb0: %s\n", strerror(errno));
    return 1;
  }

  if(ioctl(fd, FBIOBLANK, mode) != 0) {
    fprintf(stderr, "FBIOBLANK(%d) failed: %s\n", mode, strerror(errno));
    close(fd);
    return 1;
  }

  printf("FBIOBLANK(%d) OK\n", mode);
  close(fd);
  return 0;
}
