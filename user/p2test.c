#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

//jaeuk_chocho_pa1
#define INVALID_PID 99999
#define MEM_PAGES 3
#define SCHED_TRIALS 2
#define SCHED_WORK 80000000ULL

static int failures;

static void
checkeq(char *name, long long got, long long want)
{
  if(got == want){
    printf("PASS %s got=%lld\n", name, got);
  } else {
    printf("FAIL %s got=%lld want=%lld\n", name, got, want);
    failures++;
  }
}

static void
checktrue(char *name, int cond, long long a, long long b)
{
  if(cond){
    printf("PASS %s a=%lld b=%lld\n", name, a, b);
  } else {
    printf("FAIL %s a=%lld b=%lld\n", name, a, b);
    failures++;
  }
}

static void
busywork(uint64 loops)
{
  volatile uint64 acc = 0;
  uint64 i;

  for(i = 0; i < loops; i++)
    acc += i;

  if(acc == 0xdeadbeefULL)
    printf("unreachable %lu\n", acc);
}

static void
release_and_close(int fd, char c)
{
  if(write(fd, &c, 1) != 1){
    printf("FAIL pipe release fd=%d\n", fd);
    failures++;
  }
  close(fd);
}

static void
test_self_nice_bounds(void)
{
  int self;

  printf("INFO section self_nice begin\n");
  self = getpid();
  checkeq("self default nice", getnice(self), 20);
  checkeq("setnice self to 0", setnice(self, 0), 0);
  checkeq("getnice self after 0", getnice(self), 0);
  checkeq("setnice self to 39", setnice(self, 39), 0);
  checkeq("getnice self after 39", getnice(self), 39);
  checkeq("setnice self invalid low", setnice(self, -1), -1);
  checkeq("setnice self invalid high", setnice(self, 40), -1);
  checkeq("setnice self restore 20", setnice(self, 20), 0);
  checkeq("getnice self after restore", getnice(self), 20);
  checkeq("getnice invalid pid", getnice(INVALID_PID), -1);
  checkeq("setnice invalid pid", setnice(INVALID_PID, 10), -1);
  printf("INFO section self_nice end\n");
}

static void
test_child_nice_control(void)
{
  int gate[2];
  int pid;
  int self;
  char c = 'x';

  printf("INFO section child_nice begin\n");
  if(pipe(gate) < 0){
    printf("FAIL child_nice pipe\n");
    failures++;
    return;
  }

  self = getpid();
  checkeq("setnice parent to 39 before fork", setnice(self, 39), 0);
  pid = fork();
  if(pid == 0){
    close(gate[1]);
    if(read(gate[0], &c, 1) != 1)
      exit(2);
    close(gate[0]);
    exit(0);
  }

  close(gate[0]);
  checkeq("child default nice", getnice(pid), 20);
  checkeq("setnice child to 5", setnice(pid, 5), 0);
  checkeq("getnice child after 5", getnice(pid), 5);
  checkeq("setnice child to 0", setnice(pid, 0), 0);
  checkeq("getnice child after 0", getnice(pid), 0);
  checkeq("setnice child to 39", setnice(pid, 39), 0);
  checkeq("getnice child after 39", getnice(pid), 39);
  release_and_close(gate[1], c);
  checkeq("waitpid child_nice cleanup", waitpid(pid), 0);
  checkeq("restore parent nice after fork", setnice(self, 20), 0);
  printf("INFO section child_nice end\n");
}

static void
test_ps_output(void)
{
  int gate[2];
  int pid;
  char c = 'x';

  printf("INFO section ps begin\n");
  if(pipe(gate) < 0){
    printf("FAIL ps pipe\n");
    failures++;
    return;
  }

  pid = fork();
  if(pid == 0){
    close(gate[1]);
    if(read(gate[0], &c, 1) != 1)
      exit(2);
    close(gate[0]);
    exit(0);
  }

  close(gate[0]);
  checkeq("setnice ps target child", setnice(pid, 3), 0);
  printf("INFO ps(0) begin\n");
  ps(0);
  printf("INFO ps(0) end\n");
  printf("INFO ps(valid child pid=%d) begin\n", pid);
  ps(pid);
  printf("INFO ps(valid child pid=%d) end\n", pid);
  printf("INFO ps(invalid pid=%d) begin\n", INVALID_PID);
  ps(INVALID_PID);
  printf("INFO ps(invalid pid=%d) end\n", INVALID_PID);
  release_and_close(gate[1], c);
  checkeq("waitpid ps child cleanup", waitpid(pid), 0);
  printf("INFO section ps end\n");
}

static void
test_meminfo_pages(void)
{
  uint64 before, after_alloc, after_free;
  uint64 expected_delta;
  char *p;

  printf("INFO section meminfo begin\n");
  before = meminfo();
  expected_delta = MEM_PAGES * PGSIZE;
  p = sbrk(expected_delta);
  checktrue("sbrk multi-page succeeded", p != (char *)-1, (long long)p, -1);
  after_alloc = meminfo();
  checkeq("meminfo exact delta", before - after_alloc, expected_delta);
  sbrk(-expected_delta);
  after_free = meminfo();
  checkeq("meminfo restored after free", after_free, before);
  printf("INFO section meminfo end\n");
}

static void
test_waitpid_basic(void)
{
  int pid;

  printf("INFO section waitpid_basic begin\n");
  pid = fork();
  if(pid == 0){
    pause(5);
    exit(7);
  }

  checkeq("waitpid existing child", waitpid(pid), 0);
  checkeq("waitpid same child again", waitpid(pid), -1);
  checkeq("waitpid invalid pid", waitpid(INVALID_PID), -1);
  printf("INFO section waitpid_basic end\n");
}

static void
test_waitpid_permission(void)
{
  int p[2];
  int checker;
  int target;
  int pid1, pid2;
  int st1 = -1, st2 = -1;
  int checker_status = -1;

  printf("INFO section waitpid_permission begin\n");
  if(pipe(p) < 0){
    printf("FAIL waitpid permission pipe\n");
    failures++;
    return;
  }

  checker = fork();
  if(checker == 0){
    int sibling;

    close(p[1]);
    if(read(p[0], &sibling, sizeof(sibling)) != sizeof(sibling))
      exit(2);
    close(p[0]);
    if(waitpid(sibling) == -1)
      exit(0);
    exit(1);
  }

  target = fork();
  if(target == 0){
    close(p[0]);
    close(p[1]);
    pause(20);
    exit(0);
  }

  close(p[0]);
  if(write(p[1], &target, sizeof(target)) != sizeof(target)){
    close(p[1]);
    printf("FAIL waitpid permission write\n");
    failures++;
    return;
  }
  close(p[1]);

  pid1 = wait(&st1);
  pid2 = wait(&st2);

  if(pid1 == checker)
    checker_status = st1;
  else if(pid2 == checker)
    checker_status = st2;

  checkeq("waitpid permission denied", checker_status, 0);
  printf("INFO section waitpid_permission end\n");
}

static void
run_sched_trial(int trial)
{
  int lowpipe[2];
  int highpipe[2];
  int lowpid, highpid;
  int st1 = -1, st2 = -1;
  int first, second;
  char start = 'x';
  char name[32];

  if(pipe(lowpipe) < 0 || pipe(highpipe) < 0){
    printf("FAIL scheduler trial %d pipe\n", trial);
    failures++;
    return;
  }

  lowpid = fork();
  if(lowpid == 0){
    close(lowpipe[1]);
    close(highpipe[0]);
    close(highpipe[1]);
    if(read(lowpipe[0], &start, 1) != 1)
      exit(2);
    close(lowpipe[0]);
    busywork(SCHED_WORK);
    exit(0);
  }

  highpid = fork();
  if(highpid == 0){
    close(highpipe[1]);
    close(lowpipe[0]);
    close(lowpipe[1]);
    if(read(highpipe[0], &start, 1) != 1)
      exit(2);
    close(highpipe[0]);
    busywork(SCHED_WORK);
    exit(0);
  }

  close(lowpipe[0]);
  close(highpipe[0]);
  checkeq("setnice low child", setnice(lowpid, 0), 0);
  checkeq("setnice high child", setnice(highpid, 39), 0);
  release_and_close(lowpipe[1], start);
  release_and_close(highpipe[1], start);

  first = wait(&st1);
  second = wait(&st2);

  printf("INFO scheduler trial=%d first_exit=%d lowpid=%d highpid=%d second_exit=%d\n",
         trial, first, lowpid, highpid, second);
  name[0] = 's';
  name[1] = 'c';
  name[2] = 'h';
  name[3] = 'e';
  name[4] = 'd';
  name[5] = '_';
  name[6] = 't';
  name[7] = 'r';
  name[8] = 'i';
  name[9] = 'a';
  name[10] = 'l';
  name[11] = '_';
  name[12] = '0' + trial;
  name[13] = 0;
  checkeq(name, first, lowpid);
}

static void
test_scheduler(void)
{
  int i;

  printf("INFO section scheduler begin\n");
  for(i = 0; i < SCHED_TRIALS; i++)
    run_sched_trial(i);
  printf("INFO section scheduler end\n");
}

int
main(void)
{
  printf("p2test start\n");

  test_self_nice_bounds();
  test_child_nice_control();
  test_ps_output();
  test_meminfo_pages();
  test_waitpid_basic();
  test_waitpid_permission();
  test_scheduler();

  if(failures == 0)
    printf("p2test done: PASS\n");
  else
    printf("p2test done: FAIL count=%d\n", failures);

  exit(failures);
}
