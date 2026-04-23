#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void die(const char *msg) {
  perror(msg);
  _exit(1);
}

int main(int argc, char **argv) {
  const char *dev = (argc >= 2) ? argv[1] : "/dev/fb0";
  int fd = open(dev, O_RDONLY);
  if (fd < 0) die("open");

  struct fb_var_screeninfo v;
  memset(&v, 0, sizeof(v));
  if (ioctl(fd, FBIOGET_VSCREENINFO, &v) != 0) die("FBIOGET_VSCREENINFO");

  struct fb_fix_screeninfo f;
  memset(&f, 0, sizeof(f));
  if (ioctl(fd, FBIOGET_FSCREENINFO, &f) != 0) die("FBIOGET_FSCREENINFO");

  printf("dev=%s\n", dev);
  printf("xres=%u yres=%u bpp=%u\n", v.xres, v.yres, v.bits_per_pixel);
  printf("xres_virtual=%u yres_virtual=%u\n", v.xres_virtual, v.yres_virtual);
  printf("xoffset=%u yoffset=%u\n", v.xoffset, v.yoffset);
  printf("line_length=%u smem_len=%u\n", f.line_length, f.smem_len);

  close(fd);
  return 0;
}
