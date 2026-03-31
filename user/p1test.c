#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

//jaeuk_chocho_pa1
#define INVALID_PID 99999
#define SCHED_WORK 50000000ULL

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
busywork(void)
{
  volatile uint64 acc = 0;
  uint64 i;

  for(i = 0; i < SCHED_WORK; i++)
    acc += i;

  if(acc == 0xdeadbeefULL)
    printf("unreachable %lu\n", acc);
}

static void
test_ps(int self)
{
  printf("INFO ps(0) begin\n");
  ps(0);
  printf("INFO ps(0) end\n");

  printf("INFO ps(valid pid=%d) begin\n", self);
  ps(self);
  printf("INFO ps(valid pid=%d) end\n", self);

  printf("INFO ps(invalid pid=%d) begin\n", INVALID_PID);
  ps(INVALID_PID);
  printf("INFO ps(invalid pid=%d) end\n", INVALID_PID);
}

static void
test_waitpid_success(void)
{
  int pid;
  int ret;

  pid = fork();
  if(pid == 0){
    pause(5);
    exit(7);
  }

  ret = waitpid(pid);
  checkeq("waitpid existing child", ret, 0);
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
}

static void
test_sched_sanity(void)
{
  int lowpipe[2];
  int highpipe[2];
  int lowpid, highpid;
  int st1 = -1, st2 = -1;
  int first, second;
  char start = 'x';

  if(pipe(lowpipe) < 0 || pipe(highpipe) < 0){
    printf("FAIL scheduler sanity pipe\n");
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
    busywork();
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
    busywork();
    exit(0);
  }

  close(lowpipe[0]);
  close(highpipe[0]);
  checkeq("setnice low child", setnice(lowpid, 0), 0);
  checkeq("setnice high child", setnice(highpid, 39), 0);
  write(lowpipe[1], &start, 1);
  write(highpipe[1], &start, 1);
  close(lowpipe[1]);
  close(highpipe[1]);

  first = wait(&st1);
  second = wait(&st2);

  printf("INFO scheduler first_exit=%d lowpid=%d highpid=%d second_exit=%d\n",
         first, lowpid, highpid, second);
  checkeq("scheduler lower nice preference", first, lowpid);
}

int
main(void)
{
  int self;
  uint64 before, after_alloc, after_free;
  char *p;

  printf("p1test start\n");

  self = getpid();
  checkeq("getnice(self default)", getnice(self), 20);
  checkeq("getnice(invalid pid)", getnice(INVALID_PID), -1);

  checkeq("setnice(self, 10)", setnice(self, 10), 0);
  checkeq("getnice(self after setnice)", getnice(self), 10);
  checkeq("setnice(invalid pid)", setnice(INVALID_PID, 10), -1);
  checkeq("setnice(invalid low)", setnice(self, -1), -1);
  checkeq("setnice(invalid high)", setnice(self, 40), -1);
  checkeq("setnice(self restore default)", setnice(self, 20), 0);

  test_ps(self);

  before = meminfo();
  p = sbrk(PGSIZE);
  checktrue("sbrk(PGSIZE) succeeded", p != (char *)-1, (long long)p, -1);
  after_alloc = meminfo();
  checktrue("meminfo decreases after alloc", after_alloc < before,
            after_alloc, before);
  sbrk(-PGSIZE);
  after_free = meminfo();
  checkeq("meminfo restored after free", after_free, before);

  test_waitpid_success();
  checkeq("waitpid(invalid pid)", waitpid(INVALID_PID), -1);
  test_waitpid_permission();

  test_sched_sanity();

  if(failures == 0)
    printf("p1test done: PASS\n");
  else
    printf("p1test done: FAIL count=%d\n", failures);

  exit(failures);
}
