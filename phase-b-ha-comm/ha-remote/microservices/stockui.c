#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stockui.h"

static int read_pidfile_int(const char *path)
{
  FILE *f = fopen(path, "r");
  if(!f) return -1;

  char buf[64];
  if(!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
  fclose(f);

  errno = 0;
  long v = strtol(buf, NULL, 10);
  if(errno != 0) return -1;
  if(v <= 1 || v > 999999) return -1;
  return (int)v;
}

void stockui_restart_via_pidfile(void)
{
  const char *pidpath = "/var/run/squeezeplay.pid";
  int pid = read_pidfile_int(pidpath);

  if(pid > 1) {
    /* User-verified behavior: killing the watchdog pid + removing pidfile
       causes the real stock UI to restart cleanly. */
    kill(pid, SIGTERM);
    usleep(200 * 1000);
  }

  unlink(pidpath);
}
