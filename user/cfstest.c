// cfstest: CPU share by nice value (make qemu SCHEDULER=CFS CPUS=1).
//
// Children with nice -5, 0, 5 and 10 count in a loop for the same
// time (measured with uptime()). Under CFS the counts should follow
// the weights 3121 : 1024 : 335 : 110, i.e. about x3 for every 5 nice
// steps. Under RR (SCHEDULER=RR) all counts should be about equal.
// Press Ctrl+P while it runs to see nice and vruntime of each process.
//
// The share checks assume one CPU.

#include "kernel/types.h"
#include "user/user.h"

#define NCHILD 4
#define RUNTICKS 200

static int nices[NCHILD] = { -5, 0, 5, 10 };
static int weights[NCHILD] = { 3121, 1024, 335, 110 };
static int failures;

static void
check(int ok, char *msg)
{
  printf("%s: %s\n", ok ? "OK  " : "FAIL", msg);
  if (!ok)
    failures++;
}

// print a/b with two decimals
static void
ratio(uint64 a, uint64 b)
{
  if (b == 0) {
    printf("inf");
    return;
  }
  uint64 r = a * 100 / b;
  printf("%d.%d%d", (int)(r / 100), (int)(r / 10 % 10), (int)(r % 10));
}

struct result {
  int idx;
  uint64 count;
};

int
main(void)
{
  int start[2], res[2], pids[NCHILD], i, end;
  uint64 counts[NCHILD];
  struct result r;

#ifdef CFS
  printf("cfstest: scheduler CFS\n");
#elif defined(MLFQ)
  printf("cfstest: scheduler MLFQ (share checks are for CFS/RR)\n");
#else
  printf("cfstest: scheduler RR\n");
#endif

  // ---- setnice argument checks
  check(setnice(getpid(), 20) == -1, "setnice(nice = 20) returns -1");
  check(setnice(getpid(), -21) == -1, "setnice(nice = -21) returns -1");
  check(setnice(12345, 0) == -1, "setnice(unknown pid) returns -1");
  check(setnice(getpid(), 0) == 0, "setnice(self, 0) returns 0");

  pipe(start);
  pipe(res);
  for (i = 0; i < NCHILD; i++) {
    if ((pids[i] = fork()) == 0) {
      close(start[1]);
      close(res[0]);
      // wait for the start signal (the common end tick), then count.
      // all children stop at the same tick, so none runs alone at
      // the end.
      if (read(start[0], &end, sizeof(end)) != sizeof(end))
        exit(1);
      volatile uint64 n = 0;
      while (uptime() < end)
        for (int k = 0; k < 1000; k++)
          n++;
      r.idx = i;
      r.count = n;
      write(res[1], &r, sizeof(r));
      exit(0);
    }
    if (setnice(pids[i], nices[i]) != 0)
      check(0, "setnice(child) returns 0");
  }
  close(start[0]);
  close(res[1]);

  printf("children:");
  for (i = 0; i < NCHILD; i++)
    printf(" pid %d nice %d (weight %d);", pids[i], nices[i], weights[i]);
  printf("\ncounting for %d ticks...\n", RUNTICKS);

  // start all children at once with the same end tick.
  end = uptime() + RUNTICKS;
  for (i = 0; i < NCHILD; i++)
    write(start[1], &end, sizeof(end));
  close(start[1]);
  for (i = 0; i < NCHILD; i++) {
    if (read(res[0], &r, sizeof(r)) != sizeof(r)) {
      printf("cfstest: lost a result\n");
      exit(1);
    }
    counts[r.idx] = r.count;
  }
  for (i = 0; i < NCHILD; i++)
    wait(0);

  uint64 total = 0;
  for (i = 0; i < NCHILD; i++)
    total += counts[i];

  printf("\nnice  weight  count        share   expected(CFS)\n");
  for (i = 0; i < NCHILD; i++) {
    printf("%d%s  %d%s  %lu   ", nices[i], nices[i] < 0 ? "" : " ",
           weights[i], weights[i] >= 1000 ? "  " : (weights[i] >= 100 ? "   " : "    "),
           counts[i]);
    printf("%d%%     %d%%\n", (int)(counts[i] * 100 / total),
           weights[i] * 100 / (3121 + 1024 + 335 + 110));
  }
  printf("\nratios (CFS expects about 3.05 each):\n");
  for (i = 0; i + 1 < NCHILD; i++) {
    printf("  nice %d / nice %d = ", nices[i], nices[i + 1]);
    ratio(counts[i], counts[i + 1]);
    printf("   (weights: ");
    ratio(weights[i], weights[i + 1]);
    printf(")\n");
  }

  // ---- checks: each ratio within a generous band
  for (i = 0; i + 1 < NCHILD; i++) {
    uint64 a = counts[i], b = counts[i + 1];
#ifdef CFS
    // expected about 3.05; accept 2.0 .. 4.5
    check(a * 10 >= b * 20 && a * 10 <= b * 45,
          "count ratio follows the weight ratio (2.0 .. 4.5)");
#else
    // RR ignores nice: expect about 1; accept 0.7 .. 1.4
    check(a * 10 >= b * 7 && a * 10 <= b * 14,
          "count ratio is about 1 (nice is ignored)");
#endif
  }

  if (failures == 0)
    printf("cfstest: all checks passed\n");
  else
    printf("cfstest: %d checks FAILED\n", failures);
  exit(failures != 0);
}
