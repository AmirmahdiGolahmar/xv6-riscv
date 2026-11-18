#define NPROC       64                // maximum number of processes
#define NCPU        8                 // maximum number of CPUs
#define NOFILE      16                // open files per process
#define NFILE       100               // open files per system
#define NINODE      50                // maximum number of active i-nodes
#define NDEV        10                // maximum major device number
#define ROOTDEV     1                 // device number of file system root disk
#define MAXARG      32                // max exec arguments
#define MAXOPBLOCKS 10                // max # of blocks any FS op writes
#define LOGBLOCKS   (MAXOPBLOCKS * 3) // max data blocks in on-disk log
#define NBUF        (MAXOPBLOCKS * 3) // size of disk block cache
#define FSSIZE      2000              // size of file system in blocks
#define MAXPATH     128               // maximum file path name
#define USERSTACK   1                 // user stack pages

// scheduling (Part 2)
#define NQUEUE          3   // MLFQ queues: 1 = RR (highest), 2 = SJF, 3 = FCFS
#define RR_QUANTUM      5   // ticks a queue-1 (or RR-mode) process may run
#define SJF_SLICE       1   // ticks between SJF decisions in queue 2
#define AGING_THRESHOLD 800 // RUNNABLE ticks before promotion to the next queue
#define DEFAULT_BURST   2   // default SJF burst estimate
#define DEFAULT_CONF    50  // default SJF confidence (0..100)

// CFS (Part 3)
#define CFS_PERIOD      12   // scheduling period L, ticks
#define CFS_MIN_GRAN    2    // minimum slice, ticks
#define NICE_MIN        (-20)
#define NICE_MAX        19
#define NICE_0_WEIGHT   1024
#define VRT_SCALE       1000 // vruntime is kept in 1/1000 tick
