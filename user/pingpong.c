#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char byte = 'a';
  int p[2];  /* pipe fds */

  if (pipe(p) < 0) {
    fprintf(2, "Error occurs when creating a pipe");
    exit(1);
  }
  if (fork() == 0) {
    /* child process */
    if (read(p[0], &byte, 1) < 0) {
      fprintf(2, "Read error from child process.\n");
      exit(1);
    }
    else {
      printf("%d: received ping\n", getpid());
    }

    if (write(p[1], &byte, 1) < 0) {
      fprintf(2, "Error occurs while writing pong to parent\n");
      exit(1);
    }

  } else {
    /* parent process */
    if (write(p[1], &byte, 1) < 0) {
      fprintf(2, "Error occurs while writing ping to child\n");
      exit(1);
    }

    if (read(p[0], &byte, 1) < 0) {
      fprintf(2, "Read error from parent process.\n");
      exit(1);
    }
    else {
      printf("%d: received pong\n", getpid());
    }
  }
  
  exit(0);
}
