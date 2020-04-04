#include <stdlib.h>
#include <stdio.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usyscall.h>
#include <libuser.h>
#include <sys_call.h>

//semaphore 	running;

/* Function Prototypes. */
int start2(char *);
extern int start3(char *);
static void spawn(sysargs *args_ptr);
int spawn_real(char *name, int (*func)(char *), char *arg, int stack_size, int priority);
static void wait_handler(sysargs *args_ptr);
int wait_real(int *status);
static void terminate(sysargs *args_ptr);
void terminate_real(int exit_status);
static void nullsys3(sysargs *args_ptr);
static int spawn_launch(char *arg);

/* Phase 3 Process Table Array */
proc_struct ProcTable[MAXPROC];

int start2(char *arg)
{
    int		pid;
    int		status;
    int   i;

    /* Check kernel mode here. */

    /* Data structure initialization as needed... */

    /* Initializes all entries to the the system vector table to nullsys3. */
    for(i = 0; i < MAXSYSCALLS; i++)
    {
      sys_vec[i] = nullsys3;
    }

    /*
     * Sets all relevant entries in the system vector table to their
     * appropriate handler function. Leaves the rest as invalid pointing to
     * nullsys3.
     */
    sys_vec[SYS_SPAWN]        = (void *) spawn;
    sys_vec[SYS_WAIT]         = (void *) wait_handler;
    sys_vec[SYS_TERMINATE]    = (void *) terminate;

    /* Initializes the Phase 3 Process Table. */
    for(i = 0; i < MAXPROC; i++)
    {
      ProcTable[i].next_proc = NULL;
      ProcTable[i].child_ptr = NULL;
      ProcTable[i].sibling_ptr = NULL;
      ProcTable[i].pid = INIT_VAL;
      ProcTable[i].priority = INIT_VAL;
      ProcTable[i].status = ITEM_EMPTY;
      ProcTable[i].start_mbox = 0;
    }
    
    /*
     * Create first user-level process and wait for it to finish.
     * These are lower-case because they are not system calls;
     * system calls cannot be invoked from kernel mode.
     * Assumes kernel-mode versions of the system calls
     * with lower-case names.  I.e., Spawn is the user-mode function
     * called by the test cases; spawn is the kernel-mode function that
     * is called by the syscall_handler; spawn_real is the function that
     * contains the implementation and is called by spawn.
     *
     * Spawn() is in libuser.c.  It invokes usyscall()
     * The system call handler calls a function named spawn() -- note lower
     * case -- that extracts the arguments from the sysargs pointer, and
     * checks them for possible errors.  This function then calls spawn_real().
     *
     * Here, we only call spawn_real(), since we are already in kernel mode.
     *
     * spawn_real() will create the process by using a call to fork1 to
     * create a process executing the code in spawn_launch().  spawn_real()
     * and spawn_launch() then coordinate the completion of the phase 3
     * process table entries needed for the new process.  spawn_real() will
     * return to the original caller of Spawn, while spawn_launch() will
     * begin executing the function passed to Spawn. spawn_launch() will
     * need to switch to user-mode before allowing user code to execute.
     * spawn_real() will return to spawn(), which will put the return
     * values back into the sysargs pointer, switch to user-mode, and 
     * return to the user code that called Spawn.
     */
    pid = spawn_real("start3", start3, NULL, 4*USLOSS_MIN_STACK, 3);
    pid = wait_real(&status);
    quit(0);

    return 0;

} /* start2 */

static void nullsys3(sysargs *args_ptr)
{
  printf("nullsys3(): Invalid syscall %d\n", args_ptr->number);
  printf("nullsys3(): process %d terminating \n", getpid());
  terminate_real(1);
}/* nullsys3 */

static void spawn(sysargs *args_ptr)
{
  int (*func)(char *);
  char *arg;
  int stack_size;
  int kid_pid;
  char *name;
  int priority;
  //more local variables

  if(is_zapped())
    terminate_real(0);

  func = args_ptr->arg1;
  arg  = args_ptr->arg2;
  stack_size = (int) args_ptr->arg3;
  priority = (int) args_ptr->arg4;
  name = args_ptr->arg5;
  //more code to extract system call arguments
  //exceptional conditions checking and handling
  //call another function to modularize the code better
  kid_pid = spawn_real(name, func, arg, stack_size, priority);
  args_ptr->arg1 = (void *) kid_pid;
  args_ptr->arg4 = (void *) 0;

  if(is_zapped())
    terminate_real(0);
  psr_set(psr_get() & ~PSR_CURRENT_MODE);
  return;
}

int spawn_real(char *name, int (*func)(char *), char *arg, int stack_size, int priority)
{
  int kidpid;
  int my_location;    /* Parent Process' location in the process table. */
  int kid_location;   /* Child Process' location in the process table. */
  int result;
  //int (* start_func)(char *arg) = spawn_launch(func);
  //u_proc_ptr kidptr, prev_ptr; /* Unused for now */

  my_location = getpid() % MAXPROC;
  //prev_ptr = &ProcTable[my_location];

  /* create our child */
  kidpid = fork1(name, spawn_launch, NULL, stack_size, priority);
  
  kid_location = kidpid % MAXPROC;
  //printf("kidpid: %d\n", kid_location);
  //kidptr = &ProcTable[kid_location];

  /* Temporary */
  ProcTable[kid_location].start_mbox = MboxCreate(0,sizeof(int));
  ProcTable[kid_location].start_func = func;
  ProcTable[kid_location].start_arg = arg;

  /* 
   * more to check the kidpid and put the new process data to the process table.
   * Then synchronize with the child using a mailbox: 
   */
  //printf("is it?\n");
  result = MboxSend(ProcTable[kid_location].start_mbox, &my_location, sizeof(int));

  /* More to add. */
  return kidpid;
}/* spawn_real */

static int spawn_launch(char *arg)
{
  //printf("spawn_launch.\n");
  int     parent_location = 0; /* Unused for now */
  int     my_location;
  int     result;
  int     (* start_func) (char *);
  char    *start_arg;
  /* add more if I deem it necessary */

  my_location = getpid() % MAXPROC;
  //printf("PID: %d\n", my_location);

  /* Sanity Check */
  /* Maintain the process table entry, you can add more */
  ProcTable[my_location].status = ITEM_IN_USE;

  /* 
   * You should synchronize with the parent here, which function to call?
   * receive?
   */
  //printf("causes deadlock..\n");
  MboxReceive(ProcTable[my_location].start_mbox, &parent_location, sizeof(int));
  //printf("parent: %d\n", parent_location);

  /* Then get the start function and its arguments. */
  //start_func = start3;
  start_func = ProcTable[my_location].start_func;
  start_arg = ProcTable[my_location].start_arg;

  if(!is_zapped())
  {
    /*add more code if I deem it necessary. */
    /* sets up user mode */
    psr_set(psr_get() & ~PSR_CURRENT_MODE);
    //printf("if\n");
    result = (start_func)(start_arg);
    //printf("result: %d\n", result);
    Terminate(result);
  }
  else
  {
    terminate_real(0);
  }
  
  printf("spawn_launch(): should not see this message following Terminate!\n");

  return 0;
}/* spawn_launch */

static void terminate(sysargs *args_ptr)
{
  int exit_status;

  exit_status = (int) args_ptr->arg1;
  terminate_real(exit_status);
}

void terminate_real(int exit_status)
{
  //printf("term real\n");
  int my_location = getpid() % MAXPROC;
  MboxRelease(ProcTable[my_location].start_mbox);
  quit(exit_status);
}

static void wait_handler(sysargs *args_ptr)
{
  int status = 0;
  args_ptr->arg1 = (void *) wait_real(&status);
  args_ptr->arg2 = (void *) status;
}

int wait_real(int *status)
{
  int pid;
  pid = join(status);
  if(is_zapped())
    terminate_real(0);
  return pid;
}
