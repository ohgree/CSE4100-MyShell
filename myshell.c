/* $begin shellmain */
#include "myshell.h"
#include <errno.h>
#define INVALID_ID -1
#define MAXARGS 128
#define MAXJOBS 32
#define PATH_PLACEHOLDER "/bin:/usr/bin"
#define PS1 "CSE4100-SP-P#1>"

typedef enum _job_state {
  INVALID = 0,
  RUNNING,
  STOPPED,
  BACKGROUND,
} job_state;

typedef struct _job {
  int id;
  pid_t pid;
  job_state state;
  char cmd[MAXLINE];
} job;

job jobs[MAXJOBS];

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv);

int exec_cmdline(char **argv, const char *cmdline, int bg);
void exec_pipeline(const char **cmds[], size_t pos, int in_fd);

void append_path(const char *path);

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

void show_jobs(void);
void init_jobs(void);

int delete_job_by_id(int id);
job *get_job_by_pid(int pid);
job *get_fg_job(void);
int add_job(char *cmdline, pid_t pid, int state);

int main() {
  char cmdline[MAXLINE]; /* Command line */

  /* Register signal handlers */
  Signal(SIGCHLD, sigchld_handler);
  Signal(SIGINT, sigint_handler);
  Signal(SIGTSTP, sigtstp_handler);

  /* Ignored signals */
  Signal(SIGTTOU, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);

  init_jobs();
  append_path(PATH_PLACEHOLDER);

  while (1) {
    /* Read */
    printf("%s ", PS1);
    fgets(cmdline, MAXLINE, stdin);
    if (feof(stdin))
      exit(0);

    /* Evaluate */
    eval(cmdline);
  }
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) {
  char *argv[MAXARGS]; /* Argument list execve() */
  char buf[MAXLINE];   /* Holds modified command line */
  int bg;              /* Should the job run in bg or fg? */
  pid_t pid;           /* Process id */

  strcpy(buf, cmdline);
  bg = parseline(buf, argv);
  if (argv[0] == NULL)
    return;                     /* Ignore empty lines */
  if (!builtin_command(argv)) { // quit -> exit(0), & -> ignore, other -> run
    exec_cmdline(argv, cmdline, bg);
  }
  return;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) {
  if (!strcmp(argv[0], "quit")) /* quit command */
    exit(0);
  if (!strcmp(argv[0], "&")) /* Ignore singleton & */
    return 1;

  /* Newly added builtins */
  // builtin:cd
  if (!strcmp(argv[0], "cd")) {
    // FIXME: Return home - Need more robust implementation
    if (!argv[1] || !strcmp(argv[1], "~")) {
      // NOTE: chdir returns -1 on error, 0 otherwise
      if (chdir(getenv("HOME")) == -1) {
        printf("cd: invalid home directory\n");
      }
    } else if (chdir(argv[1]) == -1) {
      printf("cd: No such file or directory\n");
    }
    return 1;
  }
  // builtin:exit
  if (!strcmp(argv[0], "exit"))
    exit(0);

  // job-related builtins
  if (!strcmp(argv[0], "jobs")) {
    show_jobs();
    return 1;
  }
  // if (!strcmp(argv[0], "bg")) {
  //   int job_id = get_fbg_id(argv); //	translate argv
  //   if (job_id <= 0 || job_list[job_id].pid <= 0) {
  //     printf("there is no such job\n");
  //     return 1;
  //   }
  //   if (kill(-job_list[job_id].pid, SIGCONT) < 0) {
  //     printf("kill signal error\n");
  //     return 1;
  //   }
  //   job_list[job_id].state = 'B'; // background
  //   printf("[%d]\tRunning\t\t%s", job_id, job_list[job_id].cmdline);
  //   return 1;
  // }
  // if (!strcmp(argv[0], "fg")) {
  //   sigset_t mask_one, prev;
  //   Sigemptyset(&mask_one);
  //   Sigemptyset(&prev);
  //   Sigaddset(&mask_one, SIGCHLD);
  //   Sigprocmask(SIG_BLOCK, &mask_one, NULL);
  //   pid_t pid;
  //   int job_id = get_fbg_id(argv);
  //   if (job_id < 0 || job_list[job_id].pid <= 0) {
  //     printf("there is no such job\n");
  //     return 1;
  //   }
  //   pid = job_list[job_id].pid;
  //   job_list[job_id].state = 'F'; // foreground
  //   printf("%s", job_list[job_id].cmdline);
  //   if (kill(-job_list[job_id].pid, SIGCONT) < 0) {
  //     printf("kill signal error\n");
  //     return 1;
  //   }
  //   while (job_list[job_id].pid == pid && job_list[job_id].state == 'F') {
  //     Sigsuspend(&prev); // check state change
  //   }
  //   // printf("suspend out\n");
  //   return 1;
  // }
  if (!strcmp(argv[0], "kill")) {
    int id;
    pid_t pid;
    if (argv[1] == NULL)
      printf("missing option\n");
    else if (argv[1][0] == '%') {
      // check if job id is within range
      if ((id = atoi(argv[1] + 1)) <= 0) {
        printf("wrong option\n");
        return 1;
      }
      pid = jobs[id].pid;
    } else {
      // check if pid is within range
      if ((pid = (pid_t)atoi(argv[1])) <= 0) {
        printf("wrong pid\n");
        return 1;
      }
    }
    // send SIGINT
    if (kill(-pid, SIGINT) < 0) {
      printf("SIGINT error\n");
      return 1;
    }
    return 1;
  }
  return 0; /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) {
  char *delim;          /* Points to first space delimiter */
  int argc;             /* Number of args */
  int bg;               /* Background job? */
  char next_char = ' '; /* next character to look for */

  // NOTE: Added check for newline
  if (buf[strlen(buf) - 1] == '\n')
    buf[strlen(buf) - 1] = ' '; /* Replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* Ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  while (delim = strchr(buf, next_char)) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* Ignore spaces */
      buf++;

    // NOTE: Add quotes support
    if (*buf == '\'' || *buf == '\"') {
      next_char = *buf;
      buf++;
    }
  }
  argv[argc] = NULL;

  if (argc == 0) /* Ignore blank line */
    return 1;

  /* Should the job run in the background? */
  if ((bg = (*argv[argc - 1] == '&')) != 0)
    argv[--argc] = NULL;

  return bg;
}
/* $end parseline */

/* execute command line with pipeline support */
int exec_cmdline(char **argv, const char *cmdline, int bg) {
  int fd[2];
  pid_t pid;
  size_t cmd_idx = 0;
  char buf[MAXLINE];    /* Holds modified command line */
  char **cmds[MAXARGS]; /* NULL-terminated array */

  sigset_t mask_all, mask_one, prev_mask;

  job_state state = bg ? BACKGROUND : RUNNING;

  /* Split command line by "|" and parse each lines */
  strncpy(buf, cmdline, strlen(cmdline) + 1);
  for (char *cmd = strtok(buf, "|"); cmd != NULL; cmd = strtok(NULL, "|")) {
    printf("DBG: on line [%s]\n", cmd);
    char *argv[MAXARGS];
    parseline(cmd, argv);
    cmds[cmd_idx] = calloc(MAXARGS, sizeof(char *));
    for (size_t i = 0; argv[i] != NULL; i++) {
      printf("[%s]", argv[i]);
      cmds[cmd_idx][i] = argv[i];
    }
    printf("\n");
    cmd_idx++;
  }
  cmds[cmd_idx] = NULL;

  Sigfillset(&mask_all);
  Sigemptyset(&prev_mask);

  Sigemptyset(&mask_one);
  Sigaddset(&mask_one, SIGCHLD);

  if (!(pid = Fork())) {
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    Setpgid(0, 0);
    exec_pipeline(cmds, 0, STDIN_FILENO);
  }

  /* Parent waits for foreground job to terminate */
  Sigprocmask(SIG_BLOCK, &mask_one, NULL);
  add_job(cmdline, pid, state);
  Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
  job *j = get_job_by_pid(pid);
  if (!bg) {
    if (j->id < 0) {
      /* free dynamically allocated memory */
      for (size_t i = 0; i < cmd_idx; i++) {
        free(cmds[i]);
      }
      return 0;
    }
    while (j->state == RUNNING && j->pid == pid) {
      Sigsuspend(&prev_mask);
    }
  } else { // when there is background process!
    printf("[%d] %d %s", j->id, pid, cmdline);
  }

  /* free dynamically allocated memory */
  for (size_t i = 0; i < cmd_idx; i++) {
    free(cmds[i]);
  }
  return 0;
}

/* execute pipeline by recursion */
void exec_pipeline(const char **cmds[], size_t pos, int in_fd) {
  pid_t pid;
  int fd[2];
  int status;

  /* handle last iteration */
  if (cmds[pos + 1] == NULL) {
    Dup2(in_fd, STDIN_FILENO); /* in_fd read, STDOUT write (default) */
    if (execvp(cmds[pos][0], cmds[pos]) == -1) { // ex) /bin/ls ls -al &
      printf("%s: Command not found.\n", cmds[pos][0]);
      exit(0);
    }
    return;
  }

  /* create pipe */
  if (pipe(fd) == -1) {
    printf("pipe error\n");
    exit(1);
  }
  if (!(pid = Fork())) {
    // child process
    Close(fd[0]);
    Dup2(in_fd, STDIN_FILENO);  /* read from in_fd */
    Dup2(fd[1], STDOUT_FILENO); /* write to pipe output */
    if (execvp(cmds[pos][0], cmds[pos]) == -1) {
      printf("%s: Command not found.\n", cmds[pos][0]);
      exit(0);
    }
    exit(1);
  }

  // Close unused file descriptors
  Close(fd[1]);
  Close(in_fd);

  exec_pipeline(cmds, pos + 1, fd[0]); /* recursive tail call */
}

void append_path(const char *path) {
  char buf[MAXBUF] = "";
  char *old_path;
  if (old_path = getenv("PATH")) {
    // non-empty path env
    strcat(buf, old_path);
    strcat(buf, ":");
  }
  strcat(buf, path);
  setenv("PATH", buf, 1);
}
void init_jobs() {
  for (int i = 0; i < MAXJOBS; i++) {
    delete_job_by_id(i);
  }
}

/*** Signal handlers ***/
void sigint_handler(int sig) {
  pid_t pid;
  int olderrno = errno;

  if ((pid = get_fg_job()->pid) <= 0) {
    errno = olderrno;
    return;
  }

  if (kill(-pid, sig) < 0) {
    Sio_puts("SIGINT kill error\n");
  }
  errno = olderrno;
}

void sigtstp_handler(int sig) {
  pid_t pid;
  int olderrno = errno;

  if ((pid = get_fg_job()->pid) <= 0) {
    errno = olderrno;
    return;
  }

  if (kill(-pid, sig) < 0) {
    Sio_puts("SIGTSTP kill error\n");
  }
  errno = olderrno;
}

void sigchld_handler(int sig) {
  pid_t pid;
  int status;
  int olderrno = errno;

  while ((pid = Waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
    job *j = get_job_by_pid(pid);
    if (WIFSIGNALED(status)) {
      delete_job_by_id(j->id);
    } else if (WIFSTOPPED(status)) {
      Sio_puts("[");
      Sio_putl(j->id);
      Sio_puts("] stopped ");
      Sio_puts(j->cmd);
      j->state = STOPPED;
    } else if (WIFEXITED(status)) {
      delete_job_by_id(j->id);
    }
  }

  if ((pid = get_fg_job()->pid) <= 0) {
    errno = olderrno;
    return;
  }

  if (kill(-pid, sig) < 0) {
    Sio_puts("SIGTSTP kill error\n");
  }
  errno = olderrno;
}

/*** Job-related functions ***/
void show_jobs() {
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].id == INVALID_ID)
      continue;

    printf("[%d] ", jobs[i].id);
    switch (jobs[i].state) {
    case RUNNING:
    case BACKGROUND:
      printf("running ");
      break;
    case STOPPED:
      printf("stopped ");
      break;
    default:
      printf("invalid ");
    }
    printf("%s\n", jobs[i].cmd);
  }
}

int get_next_id() {
  int max_id = 0;
  for (int i = 0; i < MAXJOBS; i++) {
    if (max_id < jobs[i].id) {
      max_id = jobs[i].id;
    }
  }
  return max_id + 1;
}

int add_job(char *cmdline, pid_t pid, int state) {
  int id;
  if (pid < 0)
    return 0;
  id = get_next_id();
  jobs[id].id = id;
  jobs[id].pid = pid;
  jobs[id].state = state;
  strcpy(jobs[id].cmd, cmdline);
}

int delete_job_by_id(int id) {
  if (id < 0)
    return -1;
  jobs[id].pid = INVALID_ID;
  jobs[id].id = INVALID_ID;
  jobs[id].state = INVALID;
  jobs[id].cmd[0] = '\0';
  return id;
}

job *get_job_by_pid(int pid) {
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      return &jobs[i];
    }
  }
  return NULL;
}

job *get_fg_job() {
  for (int i = 0; i < MAXJOBS; i++) {
    if (jobs[i].state == RUNNING) {
      return &jobs[i];
    }
  }
  return NULL;
}

/*********************************************
 * Functions from csapp.c
 ********************************************/
/* $begin unixerror */
void unix_error(char *msg) /* Unix-style error */
{
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(0);
}
/* $end unixerror */

/* $begin forkwrapper */
pid_t Fork(void) {
  pid_t pid;

  if ((pid = fork()) < 0)
    unix_error("Fork error");
  return pid;
}
/* $end forkwrapper */

pid_t Waitpid(pid_t pid, int *iptr, int options) {
  pid_t retpid;

  if ((retpid = waitpid(pid, iptr, options)) < 0)
    unix_error("Waitpid error");
  return (retpid);
}

void Close(int fd) {
  int rc;

  if ((rc = close(fd)) < 0)
    unix_error("Close error");
}

int Dup2(int fd1, int fd2) {
  int rc;

  if ((rc = dup2(fd1, fd2)) < 0)
    unix_error("Dup2 error");
  return rc;
}

void Pause() {
  (void)pause();
  return;
}

unsigned int Sleep(unsigned int secs) { return sleep(secs); }

unsigned int Alarm(unsigned int seconds) { return alarm(seconds); }

void Setpgid(pid_t pid, pid_t pgid) {
  int rc;

  if ((rc = setpgid(pid, pgid)) < 0)
    unix_error("Setpgid error");
  return;
}

pid_t Getpgrp(void) { return getpgrp(); }

/************************************
 * Wrappers for Unix signal functions
 ***********************************/

/* $begin sigaction */
handler_t *Signal(int signum, handler_t *handler) {
  struct sigaction action, old_action;

  action.sa_handler = handler;
  sigemptyset(&action.sa_mask); /* Block sigs of type being handled */
  action.sa_flags = SA_RESTART; /* Restart syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0)
    unix_error("Signal error");
  return (old_action.sa_handler);
}
/* $end sigaction */

void Sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  if (sigprocmask(how, set, oldset) < 0)
    unix_error("Sigprocmask error");
  return;
}

void Sigemptyset(sigset_t *set) {
  if (sigemptyset(set) < 0)
    unix_error("Sigemptyset error");
  return;
}

void Sigfillset(sigset_t *set) {
  if (sigfillset(set) < 0)
    unix_error("Sigfillset error");
  return;
}

void Sigaddset(sigset_t *set, int signum) {
  if (sigaddset(set, signum) < 0)
    unix_error("Sigaddset error");
  return;
}

void Sigdelset(sigset_t *set, int signum) {
  if (sigdelset(set, signum) < 0)
    unix_error("Sigdelset error");
  return;
}

int Sigismember(const sigset_t *set, int signum) {
  int rc;
  if ((rc = sigismember(set, signum)) < 0)
    unix_error("Sigismember error");
  return rc;
}

int Sigsuspend(const sigset_t *set) {
  int rc = sigsuspend(set); /* always returns -1 */
  if (errno != EINTR)
    unix_error("Sigsuspend error");
  return rc;
}

/*************************************************************
 * The Sio (Signal-safe I/O) package - simple reentrant output
 * functions that are safe for signal handlers.
 *************************************************************/

/* Private sio functions */

/* $begin sioprivate */
/* sio_reverse - Reverse a string (from K&R) */
static void sio_reverse(char s[]) {
  int c, i, j;

  for (i = 0, j = strlen(s) - 1; i < j; i++, j--) {
    c = s[i];
    s[i] = s[j];
    s[j] = c;
  }
}

/* sio_ltoa - Convert long to base b string (from K&R) */
static void sio_ltoa(long v, char s[], int b) {
  int c, i = 0;

  do {
    s[i++] = ((c = (v % b)) < 10) ? c + '0' : c - 10 + 'a';
  } while ((v /= b) > 0);
  s[i] = '\0';
  sio_reverse(s);
}

/* sio_strlen - Return length of string (from K&R) */
static size_t sio_strlen(char s[]) {
  int i = 0;

  while (s[i] != '\0')
    ++i;
  return i;
}
/* $end sioprivate */

/* Public Sio functions */
/* $begin siopublic */

ssize_t sio_puts(char s[]) /* Put string */
{
  return write(STDOUT_FILENO, s, sio_strlen(s)); // line:csapp:siostrlen
}

ssize_t sio_putl(long v) /* Put long */
{
  char s[128];

  sio_ltoa(v, s, 10); /* Based on K&R itoa() */ // line:csapp:sioltoa
  return sio_puts(s);
}

void sio_error(char s[]) /* Put error message and exit */
{
  sio_puts(s);
  _exit(1); // line:csapp:sioexit
}
/* $end siopublic *

/*******************************
 * Wrappers for the SIO routines
 ******************************/
ssize_t Sio_putl(long v) {
  ssize_t n;

  if ((n = sio_putl(v)) < 0)
    sio_error("Sio_putl error");
  return n;
}

ssize_t Sio_puts(char s[]) {
  ssize_t n;

  if ((n = sio_puts(s)) < 0)
    sio_error("Sio_puts error");
  return n;
}

void Sio_error(char s[]) { sio_error(s); }
