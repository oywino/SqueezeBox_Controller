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
    fprintf(stderr, "usage: %s <yoffset>\n", argv[0]);
    return 2;
  }
  int yoff = atoi(argv[1]);

  int fd = open("/dev/fb0", O_RDWR);
  if(fd < 0) {
    fprintf(stderr, "open /dev/fb0: %s\n", strerror(errno));
    return 1;
  }

  struct fb_var_screeninfo v;
  if(ioctl(fd, FBIOGET_VSCREENINFO, &v) != 0) {
    fprintf(stderr, "FBIOGET_VSCREENINFO: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  if(yoff < 0 || yoff > (int)(v.yres_virtual - v.yres)) {
    fprintf(stderr, "invalid yoffset=%d (allowed 0..%d)\n", yoff, (int)(v.yres_virtual - v.yres));
    close(fd);
    return 2;
  }

  v.yoffset = (unsigned int)yoff;
  v.activate = FB_ACTIVATE_NOW;

  if(ioctl(fd, FBIOPAN_DISPLAY, &v) != 0) {
    fprintf(stderr, "FBIOPAN_DISPLAY: %s\n", strerror(errno));
    close(fd);
    return 1;
  }

  printf("panned to yoffset=%u (yres=%u yres_virtual=%u)\n", v.yoffset, v.yres, v.yres_virtual);
  close(fd);
  return 0;
}
