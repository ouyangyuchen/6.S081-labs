#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int
build_argv(char *str, char **argv_list, int MAXLIMIT)
{
  char *ptr = str;
  int words = 0;

  while (*ptr != 0) {
    /* pass the spaces */
    while (*ptr == ' ') {
      *ptr = 0;
      ptr++;
    }

    if (*ptr != 0) {
      /* new word, add to list */
      if (words >= MAXLIMIT)
        return -1;
      argv_list[words++] = ptr++;
      while (*ptr != 0 && *ptr != ' ')
        ptr++;
    }
  }

  return words;
}

int
main(int argc, char *argv[])
{
  char buf[512];    /* save current line to buffer */
  char *ptr;        /* line ptr */
  int n;            /* read return value */

  while (1) {
    ptr = buf;

    /* read each line from stdin */
    while ((n = read(0, ptr, 1)) > 0) {
      if (*ptr == '\n') {
        *ptr = 0;
        break;
      }
      else
      ptr++;
    }
    if (n < 0) {
      fprintf(2, "xargs: read byte error\n");
      exit(1);
    }
    if (n == 0)
      break;

    /* build argv list = origin argv + line arguments */
    char *new_argv[MAXARG];
    for (int i = 1; i < argc; i++) {
      new_argv[i - 1] = argv[i];
    }
    if (build_argv(buf, new_argv + argc - 1, MAXARG - argc + 1) < 0) {
      printf("xargs: too many arguments\n");
      continue;
    }

    /* fork and exec */
    if (fork() == 0) {
      if (exec(new_argv[0], new_argv) < 0) {
        fprintf(2, "xargs: exec error\n");
        exit(1);
      }
    } else {
      wait(0);
    }
  }

  exit(0);
}
