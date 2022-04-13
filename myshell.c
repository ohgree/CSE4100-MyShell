/* $begin shellmain */
#include "myshell.h"
#include <errno.h>
#define MAXARGS 128
// NOTE: Temporary environment variable
#define PATH "/bin/"
#define PATH_PLACEHOLDER "/bin"
#define PS1 "CSE4100-SP-P#1>"

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv);

int exec_cmdline(char **argv, const char *cmdline, int bg);

int main() {
  char cmdline[MAXLINE]; /* Command line */

  printf("DBG: env - %s\n", getenv("PATH"));
  setenv("PATH", PATH_PLACEHOLDER, 1);
  printf("DBG: env - %s\n", getenv("PATH"));
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
  return 0; /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) {
  char *delim; /* Points to first space delimiter */
  int argc;    /* Number of args */
  int bg;      /* Background job? */

  // NOTE: Added check for newline
  if (buf[strlen(buf) - 1] == '\n')
    buf[strlen(buf) - 1] = ' '; /* Replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* Ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  while ((delim = strchr(buf, ' '))) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* Ignore spaces */
      buf++;
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
  char buf[MAXLINE]; /* Holds modified command line */
  char *cmds[MAXARGS][MAXARGS];

  strncpy(buf, cmdline, strlen(cmdline));

  // Split command line by "|" and parse each lines
  for (char *cmd = strtok(buf, "|"); cmd != NULL; cmd = strtok(NULL, "|")) {
    printf("DBG: in commandline '%s'\n", cmd);
    char *argv[MAXARGS];
    parseline(cmd, argv);
    for (size_t i = 0; argv[i] != NULL; i++) {
      printf("DBG: argv[%d] '%s'\n", i, argv[i]);
      cmds[cmd_idx][i] = argv[i];
    }
    cmd_idx++;
  }

  // no pipeline needed
  if (cmd_idx == 1) {
    char pathname[MAXLINE] = PATH;
    // printf("DBG: %s\n", cmds[0][0]);
    strncat(pathname, cmds[0][0], MAXLINE - strlen(PATH) - 1);
    // printf("DBG: pathname %s", pathname);
    if (!(pid = Fork())) {
      if (execvp(pathname, argv) == -1) { // ex) /bin/ls ls -al &
        printf("%s: Command not found.\n", argv[0]);
        exit(0);
      }
    }
    /* Parent waits for foreground job to terminate */
    if (!bg) {
      int status;
      Waitpid(pid, &status, 0);
    } else { // when there is background process!
      printf("%d %s", pid, cmdline);
    }
    return 0;
  }
}

void exec_pipeline(const char **cmds[], size_t pos, int in_fd) {
  // handle last command
  if (cmds[pos + 1] == NULL) {
    // in_fd read, STDOUT write (default)
    Dup2(in_fd, STDIN_FILENO);
    execvp(cmds[pos][0], cmds[pos]);
    return;
  }
  /* <in_fd cmds[pos] >fd[1] | <fd[0] cmds[pos+1] ... */
  int fd[2];
  if (pipe(fd) == -1) {
    perror("pipe error\n");
    exit(1);
  }
  if (fork() == -1) {
    perror("fork error\n");
    exit(1);
  } else {
    // child = 1;
    Close(fd[0]);               /* unused */
    Dup2(in_fd, STDIN_FILENO);  /* read from in_fd */
    Dup2(fd[1], STDOUT_FILENO); /* write to fd[1] */
    execvp(cmds[pos][0], cmds[pos]);
    perror("execvp error\n");
    exit(1);
  }
  Close(fd[1]);                        /* unused */
  Close(in_fd);                        /* unused */
  exec_pipeline(cmds, pos + 1, fd[0]); /* execute the rest */
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