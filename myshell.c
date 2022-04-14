/* $begin shellmain */
#include "myshell.h"
#include <errno.h>
#define MAXARGS 128
#define PATH_PLACEHOLDER "/bin:/usr/bin"
#define PS1 "CSE4100-SP-P#1>"

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv);

int exec_cmdline(char **argv, const char *cmdline, int bg);
void exec_pipeline(const char **cmds[], size_t pos, int in_fd);

void append_path(const char *path);

int main() {
  char cmdline[MAXLINE]; /* Command line */
  char *k;

  append_path(PATH_PLACEHOLDER);

  while (1) {
    /* Read */
    printf("%s ", PS1);
    k = fgets(cmdline, MAXLINE, stdin);
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
    } else {
      next_char = ' ';
    }
  }
  if (*buf) { /* handle last remaining argument just before NULL */
    argv[argc++] = buf;
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

  /* Split command line by "|" and parse each lines */
  strncpy(buf, cmdline, strlen(cmdline) + 1);
  for (char *cmd = strtok(buf, "|"); cmd != NULL; cmd = strtok(NULL, "|")) {
    char *argv[MAXARGS];
    parseline(cmd, argv);
    cmds[cmd_idx] = calloc(MAXARGS, sizeof(char *));
    for (size_t i = 0; argv[i] != NULL; i++) {
      cmds[cmd_idx][i] = argv[i];
    }
    cmd_idx++;
  }
  cmds[cmd_idx] = NULL;

  if (!(pid = Fork())) {
    exec_pipeline((const char ***)cmds, 0, STDIN_FILENO);
  }
  /* Parent waits for foreground job to terminate */
  if (!bg) {
    int status;
    Waitpid(pid, &status, 0);
  } else { // when there is background process!
    printf("%d %s", pid, cmdline);
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
    if (execvp(cmds[pos][0], (char *const *)cmds[pos]) == -1) {
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
    if (execvp(cmds[pos][0], (char *const *)cmds[pos]) == -1) {
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