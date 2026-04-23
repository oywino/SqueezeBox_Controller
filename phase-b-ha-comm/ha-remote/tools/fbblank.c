#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void die(const char *msg) { perror(msg); _exit(1); }

int main(int argc, char **argv) {
  const char *dev = "/dev/fb0";
  int blank = 0; // FB_BLANK_UNBLANK
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <blank> [fbdev]\n", argv[0]);
    fprintf(stderr, "blank: 0=unblank, 1=normal, 4=powerdown\n");
    return 2;
  }
  blank = atoi(argv[1]);
  if (argc >= 3) dev = argv[2];

  int fd = open(dev, O_RDWR);
  if (fd < 0) die("open");

  if (ioctl(fd, FBIOBLANK, blank) != 0) die("FBIOBLANK");

  printf("FBIOBLANK(%d) OK on %s\n", blank, dev);
  close(fd);
  return 0;
}
