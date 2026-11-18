// Process information returned by the getprocs() system call.
// Shared between the kernel and user programs.
// state uses the values of enum procstate in kernel/proc.h:
// 0 UNUSED, 1 USED, 2 SLEEPING, 3 RUNNABLE, 4 RUNNING, 5 ZOMBIE.

struct pinfo {
  int pid;
  int ppid;
  int state;
  uint64 sz;
  char name[16];
  int queue;      // MLFQ queue (1..3)
  int wait;       // ticks waited while RUNNABLE
  int burst;      // SJF burst estimate
  int confidence; // SJF confidence
};
