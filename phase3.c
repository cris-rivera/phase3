/* ------------------------------------------------------------------------
 * phase3.c
 *
 * University of Arizona South
 * Computer Science 452
 *
 * Author: Cristian Rivera
 * Group: Cristian Rivera (Solo)
 *
 * ------------------------------------------------------------------------ */
#include <stdlib.h>
#include <stdio.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <usyscall.h>
#include <libuser.h>
#include <sys_call.h>

/* ------------------------- Prototypes ----------------------------------- */
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
static void timeofday(sysargs *args_ptr);
static void cpu_time(sysargs *args_ptr);
static void getPID(sysargs *args_ptr);
static void reset_block();

/* ----------------- Phase 3 Process Table Array -------------------------- */
/* Phase 3 Process Table Array */
proc_struct ProcTable[MAXPROC];

/* -------------------------- Functions ----------------------------------- */

/* ------------------------------------------------------------------------
    Name - start2
    Purpose - Initializes phase 3 process table and the interrupt vector.
              Starts start3.
    Parameters - one, default arg passed by fork1, not used here.
    Returns - one to indicate normal quit.
    Side Effects - none
    ----------------------------------------------------------------------- */
int start2(char *arg)
{
    int		pid;
    int		status;
    int   i;

    /* Check kernel mode here. */

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
    sys_vec[SYS_GETTIMEOFDAY] = (void *) timeofday;
    sys_vec[SYS_CPUTIME]      = (void *) cpu_time;
    sys_vec[SYS_GETPID]       = (void *) getPID;

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

/* ------------------------------------------------------------------------
    Name - nullsys3
    Purpose - Used to represent and invalid system call.
    Parameters - one, system arguments structure pointer. Not used. 
    Returns - none
    Side Effects - terminates calling process
    ----------------------------------------------------------------------- */
static void nullsys3(sysargs *args_ptr)
{
  printf("nullsys3(): Invalid syscall %d\n", args_ptr->number);
  printf("nullsys3(): process %d terminating \n", getpid());
  terminate_real(1);
}/* nullsys3 */

/* ------------------------------------------------------------------------
    Name - spawn
    Purpose - Spawn handler function. Sets system call arguments structure
              to their appropriate values. Calls spawn_real to actually
              spawn a process.
    Parameters - one, system arguments structure pointer.
    Returns - none
    Side Effects - terminates calling process if zapped. Changes process to
                   User Mode. 
    ----------------------------------------------------------------------- */
static void spawn(sysargs *args_ptr)
{
  int (*func)(char *);
  char *arg;
  int stack_size;
  int kid_pid;
  char *name;
  int priority;

  if(is_zapped())
    terminate_real(0);

  func = args_ptr->arg1;
  arg  = args_ptr->arg2;
  stack_size = (int) args_ptr->arg3;
  priority = (int) args_ptr->arg4;
  name = args_ptr->arg5;

  kid_pid = spawn_real(name, func, arg, stack_size, priority);
  args_ptr->arg1 = (void *) kid_pid;
  args_ptr->arg4 = (void *) 0;

  if(is_zapped())
    terminate_real(0);
  psr_set(psr_get() & ~PSR_CURRENT_MODE);
  return;
}/* spawn */

/* ------------------------------------------------------------------------
     Name - spawn_real
     Purpose - Spawns process by calling fork1, but with spawn_launch
               wrapper function. Maintains Parent-Child relationship.
               Maintains the new child's process table. Blocks/unblocks
               child in order to synchronize execution and pass function
               pointer and function argument to the child process. 
     Parameters - five, the function's name, a function pointer to the 
                  process' start function, a char pointer to the argument
                  for the start function parameter, the size of the process
                  stack, and the priority of the process.
     Returns - one, the PID of the newly spawned child process. -1 if process
               failed to fork.
     Side Effects - none
     ----------------------------------------------------------------------- */
int spawn_real(char *name, int (*func)(char *), char *arg, int stack_size, int priority)
{
  int kidpid;
  int my_location;    /* Parent Process' location in the process table. */
  int kid_location;   /* Child Process' location in the process table. */
  int result;
  u_proc_ptr walker = NULL;
  u_proc_ptr parent = NULL;
  u_proc_ptr kid    = NULL;

  my_location = getpid() % MAXPROC;
  parent = &ProcTable[my_location];

  /* create our child */
  kidpid = fork1(name, spawn_launch, NULL, stack_size, priority);

  if(kidpid == -1)
    return kidpid;
  
  kid_location = kidpid % MAXPROC;
  kid = &ProcTable[kid_location];
  
  kid->name = name;
  kid->pid = kidpid;
  kid->start_func = func;
  kid->start_arg = arg;
  kid->parent_ptr = parent;

  /* Establish/Maintain Parent-Child Relationship */
  if(parent->child_ptr == NULL)
    parent->child_ptr = kid;
  else
    {
      walker = parent->child_ptr;

      if(walker->sibling_ptr == NULL)
        walker->sibling_ptr = kid;
      else
      {
        while(walker->sibling_ptr != NULL)
          walker = walker->sibling_ptr;
        walker->sibling_ptr = kid;
      }
    }  

  /* 
   * more to check the kidpid and put the new process data to the process table.
   * Then synchronize with the child using a mailbox: 
   */
  if(kid->start_mbox == 0)
    kid->start_mbox = MboxCreate(0,sizeof(int));
  else
    result = MboxSend(kid->start_mbox, &my_location, sizeof(int));
  return kidpid;
}/* spawn_real */

/* ------------------------------------------------------------------------
      Name - spawn_launch
      Purpose - Spawn Wrapper function which takes the starting function 
                for the newly forked function, and the starting argument for 
                the starting function and places it on the Process Table 
                entry for the calling function. It then places the current
                process in user mode and executes the starting function.
      Parameters - one, character pointer which is always NULL.
      Returns - one, zero which never executes.
      Side Effects - terminate is called after the starting function
                     concludes termination.
      ----------------------------------------------------------------------- */
static int spawn_launch(char *arg)
{
  int         parent_location;
  int         my_location;
  int         result;
  int         (* start_func) (char *);
  char        *start_arg;
  u_proc_ptr  kid;

  parent_location   = 0;
  my_location       = 0;
  result            = 0;
  kid               = NULL;

  my_location = getpid() % MAXPROC;
  kid = &ProcTable[my_location];

  /* Sanity Check */
  /* Maintain the process table entry. */
  kid->status = ITEM_IN_USE;
  if(kid->start_mbox == 0)
  {
    kid->start_mbox = MboxCreate(0, sizeof(int));
    MboxReceive(kid->start_mbox, &parent_location, sizeof(int));
  }

  start_func = kid->start_func;
  start_arg = kid->start_arg;
  
  if(!is_zapped())
  {
    /* sets up user mode */
    psr_set(psr_get() & ~PSR_CURRENT_MODE);
    result = (start_func)(start_arg);
    Terminate(result);
  }
  else
  {
    terminate_real(0);
  }
  
  printf("spawn_launch(): should not see this message following Terminate!\n");

  return 0;
}/* spawn_launch */

/* ------------------------------------------------------------------------
      Name - terminate
      Purpose - Zapps all the children of the calling process, then calls
                terminate_real in order to fully terminate the calling
                process. 
      Parameters - one, pointer to the system call arguments structure.
      Returns - none
      Side Effects - none
      ----------------------------------------------------------------------- */
static void terminate(sysargs *args_ptr)
{
  int           exit_status;
  int           my_location;
  u_proc_ptr    current;
  u_proc_ptr    walker;
  u_proc_ptr    next;

  exit_status   = (int) args_ptr->arg1;
  my_location   = getpid() % MAXPROC;
  current       = &ProcTable[my_location];
  walker        = NULL;
  next          = NULL;

  /*
   * Zaps the first child of the current process.
   */
  if(current->child_ptr != NULL)
  {
    while(current->child_ptr != NULL)
    {
      walker = current->child_ptr;
      zap(walker->pid);

      if(walker->sibling_ptr != NULL)
      {
        current->child_ptr = walker->sibling_ptr;
        walker->sibling_ptr = NULL;
      }
      else
        current->child_ptr = NULL;
    }
  }

  /*
   * Terminates once the children have been zapped.
   */
  terminate_real(exit_status);
}/* terminate */

/* ------------------------------------------------------------------------
      Name - terminate_real
      Purpose - Allows calling process to change parent process' children 
                list by taking itself out of the list, and adjusting list 
                as required. Calls quit after adjustment has been made.
      Parameters - one, exit status from which terminate was called.
      Returns - none
      Side Effects - none
      ----------------------------------------------------------------------- */
void terminate_real(int exit_status)
{
  int           my_location;
  u_proc_ptr    current;
  u_proc_ptr    parent;
  u_proc_ptr    walker;
  u_proc_ptr    prev;
  u_proc_ptr    next;

  my_location   = getpid() % MAXPROC;
  current       = &ProcTable[my_location];
  parent        = current->parent_ptr;
  walker        = NULL;
  prev          = NULL;
  next          = NULL;
  
  /* Adjusts the parent's child list by deleting calling process then adjusting
   * the rest of the children as needed depending on the position of the
   * calling process in the parent process' child list.
   */
  if(parent->child_ptr != NULL)
  {
    if(parent->child_ptr->pid == current->pid)
    {
      if(parent->sibling_ptr != NULL)
        parent->child_ptr = parent->sibling_ptr;
      else
        parent->child_ptr = NULL;
    }
    else
    {
      prev = parent;
      walker = parent->sibling_ptr;
      while(walker->pid != current->pid)
      {
        prev = walker;
        walker = walker->sibling_ptr;
      }

      if(walker->sibling_ptr == NULL)
        prev->sibling_ptr = NULL;
      else
      {
        next = walker->sibling_ptr;
        prev->sibling_ptr = next;
      }
    }
  }

  MboxRelease(current->start_mbox);
  reset_block();
  quit(exit_status);
}/* terminate_real */

/* ------------------------------------------------------------------------
      Name - wait_handler
      Purpose - Handler function for the wait system call. Calls to wait real
                to initiate wait functionality, then places the returned PID
                and the exit status into the system call argument structure.
      Parameters - one, pointer to system call argument structure.
      Returns - none
      Side Effects - none
      ----------------------------------------------------------------------- */
static void wait_handler(sysargs *args_ptr)
{
  int status = 0;
  args_ptr->arg1 = (void *) wait_real(&status);
  args_ptr->arg2 = (void *) status;
}/* wait_handler */

/* ------------------------------------------------------------------------
      Name - wait_real
      Purpose - Initiates a join with the child process the calling process
                is to wait on. Join is passed the status code passed in as 
                a parameter.
      Parameters - one, the status code of the wait call.
      Returns - returns the PID of the child that quit, -2 if there are no
                children, and -1 if the process was zapped while waiting.
      Side Effects - none
      ----------------------------------------------------------------------- */
int wait_real(int *status)
{
  int pid;
  pid = join(status);
  if(is_zapped())
    terminate_real(0);
  return pid;
}/* wait_real */

/* ------------------------------------------------------------------------
      Name - timeofday
      Purpose - Gets the current time from the system clock, then places 
                that time in the system call argument structure.
      Parameters - one, pointer to system call argument structure.
      Returns - none
      Side Effects - none
      ----------------------------------------------------------------------- */
static void timeofday(sysargs *args_ptr)
{
  int time = sys_clock();
  args_ptr->arg1 = (void *) time;
}/* timeofday */

/* ------------------------------------------------------------------------
      Name - cpu_time
      Purpose - Gets the amount of time the current process has been running
                then places it into the system call argument struture.
      Parameters - one, system call argument structure pointer.
      Returns - none
      Side Effects - none
      ----------------------------------------------------------------------- */
static void cpu_time(sysargs *args_ptr)
{
  int cpu_time = readtime();
  args_ptr->arg1 = (void *) cpu_time;
}/* cpu_time */

/* ------------------------------------------------------------------------
      Name - getPID
      Purpose - Places the PID of the current running process into the system
                call argument structure, which will return the PID. 
      Parameters - one, system call argument structure pointer
      Returns - none
      Side Effects - none
      ----------------------------------------------------------------------- */
static void getPID(sysargs *args_ptr)
{
  int pid = getpid();
  args_ptr->arg1 = (void *) pid;
}/* getPID */

/* ------------------------------------------------------------------------
     Name - reset_block
     Purpose - Resets the Process Table Block to initial values.
     Parameters - none
     Returns - none
     Side Effects - none
     ----------------------------------------------------------------------- */
static void reset_block()
{
  int location                    = getpid() % MAXPROC;
  u_proc_ptr proc                 = &ProcTable[location];

  proc->start_mbox                = 0;
  proc->start_func                = NULL;
  proc->start_arg                 = NULL;
}/* reset_block */
