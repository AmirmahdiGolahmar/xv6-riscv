// pstest: test the getprocs() system call and ps.
//
// - forks sleeping and busy children (plus a grandchild) and
//   checks that getprocs() reports them with the right PPID;
// - runs ps so the table can be compared with Ctrl+P;
// - checks max smaller/larger than the process count and
//   invalid arguments.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define NSLEEP 3

static struct pinfo procs[NPROC];
static int failures;

static void
check(int ok, char *msg)
{
  printf("%s: %s\n", ok ? "OK  " : "FAIL", msg);
  if (!ok)
    failures++;
}

static struct pinfo *
find(int n, int pid)
{
  for (int i = 0; i < n; i++)
    if (procs[i].pid == pid)
      return &procs[i];
  return 0;
}

static void
runps(void)
{
  int pid = fork();
  if (pid == 0) {
    char *argv[] = { "ps", 0 };
    exec("ps", argv);
    fprintf(2, "pstest: exec ps failed\n");
    exit(1);
  }
  wait(0);
}

int
main(void)
{
  int me = getpid();
  int kids[NSLEEP + 1];
  int grandchild_pipe[2];
  int block[2]; // children block reading this pipe until we close it
  int grandchild = -1;
  char c;
  int i, n, total;

  printf("pstest: my pid is %d\n", me);

  // sleeping children. they block in read() on an empty pipe,
  // so they stay SLEEPING (pause() would wake them every tick).
  pipe(block);
  for (i = 0; i < NSLEEP; i++) {
    if ((kids[i] = fork()) == 0) {
      close(block[1]);
      read(block[0], &c, 1);
      exit(0);
    }
  }

  // a child that forks a sleeping grandchild and then busy-loops.
  // it sends the grandchild's pid back over a pipe.
  pipe(grandchild_pipe);
  if ((kids[NSLEEP] = fork()) == 0) {
    int g = fork();
    if (g == 0) {
      close(block[1]);
      read(block[0], &c, 1);
      exit(0);
    }
    close(block[1]);
    write(grandchild_pipe[1], &g, sizeof(g));
    for (;;)
      ;
  }
  read(grandchild_pipe[0], &grandchild, sizeof(grandchild));
  close(grandchild_pipe[0]);
  close(grandchild_pipe[1]);

  pause(5); // let the children reach read()

  printf("\n--- ps output (compare with Ctrl+P) ---\n");
  runps();
  printf("\npstest: press Ctrl+P now to compare (waiting 3 seconds)\n");
  pause(30);
  printf("\n");

  // all processes
  total = getprocs(procs, NPROC);
  check(total >= NSLEEP + 4, "getprocs(buf, NPROC) returns all processes");

  struct pinfo *pi = find(total, 1);
  check(pi != 0 && pi->ppid == 0, "init has ppid 0");
  pi = find(total, me);
  check(pi != 0 && pi->state == 4, "caller is RUNNING");

  for (i = 0; i < NSLEEP; i++) {
    pi = find(total, kids[i]);
    check(pi != 0 && pi->ppid == me && pi->state == 2,
          "sleeping child: ppid = pstest, SLEEPING");
  }
  pi = find(total, kids[NSLEEP]);
  check(pi != 0 && pi->ppid == me &&
        (pi->state == 3 || pi->state == 4),
        "busy child: ppid = pstest, RUNNABLE/RUNNING");
  pi = find(total, grandchild);
  check(pi != 0 && pi->ppid == kids[NSLEEP],
        "grandchild: ppid = busy child");

  // max smaller than the number of processes
  n = getprocs(procs, 2);
  check(n == 2, "getprocs(buf, 2) returns 2");

  // max larger than the number of processes
  n = getprocs(procs, NPROC * 4);
  check(n == total, "getprocs(buf, 4*NPROC) returns the full count");

  // edge cases / invalid arguments
  check(getprocs(procs, 0) == 0, "max = 0 returns 0");
  check(getprocs(procs, -1) == -1, "max < 0 returns -1");
  check(getprocs(0, NPROC) == -1, "buf = 0 returns -1");
  check(getprocs((struct pinfo *)0x3fffffe000L, NPROC) == -1,
        "buf outside user memory returns -1");

  // closing the write end makes the sleepers' read() return 0;
  // the busy child is killed. the grandchild is reparented to init.
  close(block[0]);
  close(block[1]);
  kill(kids[NSLEEP]);
  for (i = 0; i <= NSLEEP; i++)
    wait(0);

  if (failures == 0)
    printf("pstest: all tests passed\n");
  else
    printf("pstest: %d tests FAILED\n", failures);
  exit(failures != 0);
}
