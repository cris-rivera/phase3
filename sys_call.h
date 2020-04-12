#define INIT_VAL -1

typedef struct proc_struct proc_struct;
typedef struct proc_struct * u_proc_ptr;

struct proc_struct {
  u_proc_ptr        next_proc;              /* Structure pointer to the next process. For creating lists. */
  u_proc_ptr        parent_ptr;             /* Structure pointer to the parent process. */
  u_proc_ptr        child_ptr;              /* Structure pointer to the first child process. */
  u_proc_ptr        sibling_ptr;            /* Structure pointer to a sibling child of the current process. */
  short             pid;                    /* Process ID number. */
  int               priority;               /* Process priority. */
  int               status;                 /* The current status of the process denoted by enum status_code. */
  int               start_mbox;             /* Mbox ID Number. */
  int               (*start_func)(char *);  /* Function pointer to the starting function of the process. */
  char              *start_arg;             /* Character pointer to the argument of the starting function. */
  char              *name;                  /* Character string of the name of the process. */
};

/*
 * Enum to represent the status of the process.
 */
enum {
  ITEM_IN_USE,
  ITEM_EMPTY,
  ITEM_NOT_USE,
  ITEM_BLOCKED
}status_code;
