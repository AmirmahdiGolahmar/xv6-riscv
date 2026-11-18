// mlfqtest: demonstrate the MLFQ scheduler (make qemu SCHEDULER=MLFQ CPUS=1).
//
//   mlfqtest         full demo (~3.5 minutes, AGING_THRESHOLD = 800 ticks):
//     1. new processes start in queue 3; init and the shell are in queue 1.
//     2. after 800 RUNNABLE ticks, starving processes move 3 -> 2.
//     3. in queue 2, a smaller burst gets more CPU.
//     4. after another 800 ticks, starving processes move 2 -> 1.
//   mlfqtest slices  how long each process runs before it loses the CPU:
//     RR / queue 1: about RR_QUANTUM ticks; FCFS (queue 3): to the end.
//
// The timing checks assume one CPU. With more CPUs the scheduler still
// works, but several workers run at once so the numbers differ.

#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define AGING 800 // must match AGING_THRESHOLD in kernel/param.h

static struct pinfo procs[NPROC];
static int failures;

static char *states[] = {
  "UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"
};

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

// same table as ps (forking ps would put it in queue 3 behind
// the CPU-bound workers, so the test prints it itself).
static int
snapshot(char *title)
{
  int n = getprocs(procs, NPROC);

  printf("\n--- %s (tick %d) ---\n", title, uptime());
  printf("PID  PPID  STATE     SIZE   Q  WAIT  BURST  CONF  NAME\n");
  for (int i = 0; i < n; i++) {
    padint(procs[i].pid, 5);
    padint(procs[i].ppid, 6);
    pad(procs[i].state >= 0 && procs[i].state <= 5 ? states[procs[i].state]
                                                   : "???",
        10);
    padint(procs[i].sz, 7);
    padint(procs[i].queue, 3);
    padint(procs[i].wait, 6);
    padint(procs[i].burst, 7);
    padint(procs[i].confidence, 6);
    printf("%s\n", procs[i].name);
  }
  return n;
}

static int
queueof(int n, int pid)
{
  for (int i = 0; i < n; i++)
    if (procs[i].pid == pid)
      return procs[i].queue;
  return -1;
}

static int
stateof(int n, int pid)
{
  for (int i = 0; i < n; i++)
    if (procs[i].pid == pid)
      return procs[i].state;
  return -1;
}

static void
check(int ok, char *msg)
{
  printf("%s: %s\n", ok ? "OK  " : "FAIL", msg);
  if (!ok)
    failures++;
}

struct result {
  int pid;
  int count;
};

// Block until the start pipe is closed, then spin. Count loop
// iterations while uptime() is in [from, to) and report the count
// on resfd (if resfd >= 0). Keep spinning until killed.
static void
worker(int startfd, int from, int to, int resfd)
{
  char c;
  int t, count = 0;

  read(startfd, &c, 1);
  close(startfd);
  for (;;) {
    t = uptime();
    if (t >= to)
      break;
    if (t >= from)
      count++;
  }
  if (resfd >= 0) {
    struct result r = { getpid(), count };
    write(resfd, &r, sizeof(r));
  }
  for (;;)
    ;
}

static void
fulltest(void)
{
  int me = getpid();
  int hstart[2], wstart[2], res[2];
  int hog, w[3], i, j, n;
  int bursts[3] = { 2, 6, 12 };
  struct result r[3];
  int t0, from, to;

  printf("mlfqtest: pid %d. this takes about 3.5 minutes.\n", me);

  // the parent: largest burst, so it seldom disturbs the SJF workers.
  setburst(me, 1000, 50);

  pipe(hstart);
  pipe(wstart);
  pipe(res);

  // counting window: a little after the workers reach queue 2
  // (they start waiting when released, a few ticks after t0).
  t0 = uptime();
  from = t0 + AGING + 100;
  to = from + 300;

  // the hog is released first, so it is first in FCFS order.
  if ((hog = fork()) == 0) {
    close(hstart[1]);
    close(wstart[1]);
    close(res[0]);
    worker(hstart[0], from, to, -1);
  }
  for (i = 0; i < 3; i++) {
    if ((w[i] = fork()) == 0) {
      close(hstart[1]);
      close(wstart[1]);
      close(res[0]);
      worker(wstart[0], from, to, res[1]);
    }
    setburst(w[i], bursts[i], 60);
  }
  setburst(hog, 20, 50);
  close(hstart[0]);
  close(wstart[0]);
  close(res[1]);

  printf("hog %d, workers %d (burst 2) %d (burst 6) %d (burst 12), "
         "all confidence 60\n",
         hog, w[0], w[1], w[2]);

  // wait until every child has closed its copies of the start
  // pipes and is blocked (SLEEPING) in read(). otherwise the last
  // child to run would never block and would become the FCFS hog.
  for (;;) {
    n = getprocs(procs, NPROC);
    if (stateof(n, hog) == 2 && stateof(n, w[0]) == 2 &&
        stateof(n, w[1]) == 2 && stateof(n, w[2]) == 2)
      break;
    pause(1);
  }

  // ---- phase 1: everybody new is in queue 3
  n = snapshot("start");
  check(queueof(n, 1) == 1, "init is in queue 1");
  check(queueof(n, 2) == 1, "shell (pid 2) is in queue 1");
  check(queueof(n, me) == 3, "mlfqtest starts in queue 3");
  check(queueof(n, hog) == 3 && queueof(n, w[0]) == 3 &&
          queueof(n, w[1]) == 3 && queueof(n, w[2]) == 3,
        "forked children start in queue 3");

  // release (hog first, so it is first in FCFS order): the hog runs (FCFS, never preempted by the clock),
  // the workers and this process wait in queue 3.
  // nothing is printed until the end of phase 2, because a
  // console write may sleep and lose our place in the queue.
  close(hstart[1]);
  close(wstart[1]);

  // ---- phase 2: after 800 waiting ticks the workers move to queue 2
  // and share the CPU by approximate SJF. pause() keeps this process
  // RUNNABLE every tick, so it is promoted too.
  while (uptime() < to)
    pause(1);
  for (i = 0; i < 3; i++)
    read(res[0], &r[i], sizeof(r[i]));

  n = snapshot("after 800 ticks: workers promoted 3 -> 2");
  check(queueof(n, w[0]) == 2 && queueof(n, w[1]) == 2 &&
          queueof(n, w[2]) == 2,
        "workers were promoted to queue 2");
  check(queueof(n, hog) == 3, "the FCFS hog (now starving) is still in queue 3");
  check(queueof(n, me) == 2, "mlfqtest was promoted to queue 2");

  printf("\nloop iterations in queue 2 during ticks [%d, %d):\n", from, to);
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++)
      if (r[j].pid == w[i])
        printf("  pid %d burst %d: %d\n", w[i], bursts[i], r[j].count);
  int c[3];
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++)
      if (r[j].pid == w[i])
        c[i] = r[j].count;
  check(c[0] > c[1] && c[1] > c[2], "smaller burst ran more (SJF)");

  // ---- phase 3: make worker 0 always win SJF (burst 1, confidence 100).
  // the others in queue 2 (and this process) now starve and,
  // after 800 more ticks, move to queue 1.
  setburst(w[0], 1, 100);
  printf("\nworker %d now has burst 1, confidence 100: others in queue 2 "
         "starve...\n",
         w[0]);
  int deadline = uptime() + AGING + 300;
  for (;;) {
    pause(1);
    n = getprocs(procs, NPROC);
    if (queueof(n, me) == 1 || uptime() > deadline)
      break;
  }
  n = snapshot("after 800 more ticks: starving processes promoted 2 -> 1");
  check(queueof(n, me) == 1, "mlfqtest was promoted to queue 1");
  check(queueof(n, w[1]) == 1 && queueof(n, w[2]) == 1,
        "starving workers were promoted to queue 1");
  check(queueof(n, w[0]) == 2, "the favoured worker stays in queue 2");

  kill(hog);
  for (i = 0; i < 3; i++)
    kill(w[i]);
  for (i = 0; i < 4; i++)
    wait(0);
}

// ---- slices: how many ticks each process runs per turn
#define NSPIN 3
#define SPIN_TICKS 20
#define MAXSEG 16

struct segs {
  int pid;
  int n;
  int len[MAXSEG];
  int start[MAXSEG];
};

static void
slicestest(void)
{
  int p[2], i, k;
  struct segs s;

  printf("mlfqtest slices: %d CPU-bound children, each runs for %d ticks\n",
         NSPIN, SPIN_TICKS);
  pipe(p);
  for (i = 0; i < NSPIN; i++) {
    if (fork() == 0) {
      int last = -1, total = 0, t;
      close(p[0]);
      s.pid = getpid();
      s.n = 0;
      // a "segment" is a run of consecutive ticks seen by this process;
      // a jump of more than one tick means it was descheduled.
      while (total < SPIN_TICKS) {
        t = uptime();
        if (t == last)
          continue;
        if (last < 0 || t > last + 1) {
          if (s.n == MAXSEG)
            break;
          s.start[s.n] = t;
          s.len[s.n++] = 0;
        }
        s.len[s.n - 1]++;
        total++;
        last = t;
      }
      write(p[1], &s, sizeof(s));
      exit(0);
    }
  }
  close(p[1]);
  for (i = 0; i < NSPIN; i++) {
    read(p[0], &s, sizeof(s));
    printf("pid %d ran (start tick:ticks):", s.pid);
    for (k = 0; k < s.n; k++)
      printf(" %d:%d", s.start[k], s.len[k]);
    printf("\n");
  }
  for (i = 0; i < NSPIN; i++)
    wait(0);
  printf("RR / queue 1: turns of about %d ticks; FCFS: one turn each\n",
         RR_QUANTUM);
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "slices") == 0) {
    slicestest();
    exit(0);
  }
  fulltest();
  if (failures == 0)
    printf("\nmlfqtest: all checks passed\n");
  else
    printf("\nmlfqtest: %d checks FAILED\n", failures);
  exit(failures != 0);
}
