#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  int fd = open("/dev/misc/jive_mgmt", O_RDWR);
  if(fd < 0) {
    printf("open(/dev/misc/jive_mgmt) failed: %s\n", strerror(errno));
    return 1;
  }

  printf("jive_mgmt_ping: fd=%d\n", fd);

  /* Periodic write; hypothesis: any write resets LCD/backlight blank timer */
  const unsigned char b = 0;
  for(;;) {
    ssize_t n = write(fd, &b, 1);
    if(n < 0) {
      printf("write failed: %s\n", strerror(errno));
      return 2;
    }
    usleep(200000);
  }
}
