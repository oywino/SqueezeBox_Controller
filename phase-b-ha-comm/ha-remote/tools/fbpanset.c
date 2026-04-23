#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void die(const char *msg) { perror(msg); _exit(1); }

int main(int argc, char **argv) {
  const char *dev = "/dev/fb0";
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <yoffset> [fbdev]\n", argv[0]);
    return 2;
  }
  unsigned yoff = (unsigned)strtoul(argv[1], NULL, 0);
  if (argc >= 3) dev = argv[2];

  int fd = open(dev, O_RDWR);
  if (fd < 0) die("open");

  struct fb_var_screeninfo v;
  memset(&v, 0, sizeof(v));
  if (ioctl(fd, FBIOGET_VSCREENINFO, &v) != 0) die("FBIOGET_VSCREENINFO");

  unsigned max_yoff = (v.yres_virtual >= v.yres) ? (v.yres_virtual - v.yres) : 0;
  printf("before: yres=%u yres_virtual=%u yoffset=%u (max_yoffset=%u)\n",
         v.yres, v.yres_virtual, v.yoffset, max_yoff);

  if (yoff > max_yoff) {
    fprintf(stderr, "ERROR: requested yoffset=%u > max_yoffset=%u\n", yoff, max_yoff);
    return 3;
  }

  v.xoffset = 0;
  v.yoffset = yoff;
  v.activate = FB_ACTIVATE_NOW;

  if (ioctl(fd, FBIOPAN_DISPLAY, &v) != 0) die("FBIOPAN_DISPLAY");

  memset(&v, 0, sizeof(v));
  if (ioctl(fd, FBIOGET_VSCREENINFO, &v) != 0) die("FBIOGET_VSCREENINFO");
  printf("after:  yoffset=%u\n", v.yoffset);

  close(fd);
  return 0;
}
