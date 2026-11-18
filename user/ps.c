// ps: list processes using the getprocs() system call.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

// indexed by enum procstate (see kernel/pinfo.h)
static char *states[] = {
  "UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"
};

// print s left-aligned in a field of the given width.
// xv6 printf has no field widths.
static void
pad(char *s, int width)
{
  int n = strlen(s);
  printf("%s", s);
  do {
    printf(" ");
  } while (++n < width);
}

static void
padint(uint64 x, int width)
{
  char buf[24];
  int i = sizeof(buf) - 1;

  buf[i] = 0;
  do {
    buf[--i] = '0' + x % 10;
    x /= 10;
  } while (x);
  pad(buf + i, width);
}

int
main(int argc, char *argv[])
{
  static struct pinfo procs[NPROC];
  int n, i;
  char *st;

  n = getprocs(procs, NPROC);
  if (n < 0) {
    fprintf(2, "ps: getprocs failed\n");
    exit(1);
  }

  printf("PID  PPID  STATE     SIZE   Q  WAIT  BURST  CONF  NAME\n");
  for (i = 0; i < n; i++) {
    if (procs[i].state >= 0 && procs[i].state < sizeof(states) / sizeof(states[0]))
      st = states[procs[i].state];
    else
      st = "???";
    padint(procs[i].pid, 5);
    padint(procs[i].ppid, 6);
    pad(st, 10);
    padint(procs[i].sz, 7);
    padint(procs[i].queue, 3);
    padint(procs[i].wait, 6);
    padint(procs[i].burst, 7);
    padint(procs[i].confidence, 6);
    printf("%s\n", procs[i].name);
  }
  exit(0);
}
