#include "kernel/types.h"
#include "user/user.h"

int
is_prime(int num) {
  for (int i = 2; i < num; i++)
    if (num % i == 0) return 0;
  return 1;
}

void
rountine(int fd) {
  int state = 1;
  int num, n;
  int prev_fd = fd, next_fd = 1;

  while ((n = read(prev_fd, &num, 4)) > 0) {
    if (n != 4) {
      fprintf(2, "%d: read number shortcount error\n", getpid());
      exit(1);
    }
    if (state) {
      /* active, waiting to print the prime */
      if (is_prime(num)) {
        printf("prime %d\n", num);
        state = 0;  /* change to passive mode after printing */

        int p[2];
        pipe(p);
        if (fork() == 0) {
          prev_fd = p[0];
          next_fd = 1;
          close(p[1]);
          state = 1;
        }
        else {
          next_fd = p[1];
          close(p[0]);
        }
      }
    }
    else {
      /* passive, just pass the number to next process */
      write(next_fd, &num, 4);
    }
  }
  close(next_fd);
  wait(0);
}

int
main(int argc, char *argv[])
{
  /* create the first pipe */
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "Error occurs when creating a pipe in main process.\n");
    exit(1); 
  }

  /* fork a child to execute rountine */
  if (fork() == 0) {
    close(p[1]);
    rountine(p[0]);
    exit(0);
  } else {
    close(p[0]);
    /* send numbers to pipe */
    for (int i = 2; i <= 35; i++) {
      write(p[1], &i, 4);
    }
    close(p[1]);    /* after sending, close the pipe */

    wait(0);
    exit(0);
  }
}

