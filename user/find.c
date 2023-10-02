#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void search(int fd, char *buf, char *ptr, const char *pattern) {
  struct dirent subfile;
  struct stat st;
  while (read(fd, &subfile, sizeof(subfile)) == sizeof(subfile)) {
    if (subfile.inum == 0)
      continue;
    /* construct path of file */
    char *new_ptr = ptr + strlen(subfile.name);
    memmove(ptr, subfile.name, strlen(subfile.name));
    *new_ptr = 0;

    if (stat(buf, &st) < 0) {
      fprintf(2, "cannot stat %s\n", buf);
      continue;
    }
    switch (st.type) {
      case T_FILE:
        if (strcmp(subfile.name, pattern) == 0) {
          *new_ptr++ = '\n';
          *new_ptr = 0;
          printf(buf);
        }
        break;

      case T_DIR:
        if (strcmp(subfile.name, ".") == 0 || strcmp(subfile.name, "..") == 0) {
          break;
        }
        
        int subfd = open(buf, 0);
        if (subfd < 0) {
          fprintf(2, "Cannot open dir: %s\n", buf);
          break;
        }
        *new_ptr++ = '/';
        search(subfd, buf, new_ptr, pattern);    /* search sub-directory */
        break;
    }
  }
}

int
main(int argc, char *argv[])
{
  if (argc != 3) {
    fprintf(2, "Usage: find <dir> <pattern>\n<");
    exit(1);
  }
  char *dirname = argv[1];
  char *pattern = argv[2];

  int fd;
  struct stat st;

  /* validate directory */
  if ((fd = open(dirname, 0)) < 0) {
    fprintf(2, "Directory is not found\n");
    exit(1);
  }
  if ((fstat(fd, &st)) < 0) {
    fprintf(2, "Cannot get stat from directory\n");
    exit(1);
  }
  if (st.type != T_DIR) {
    fprintf(2, "Not a valid directory\n");
    exit(1);
  }

  /* build path name */
  char buf[512];
  strcpy(buf, dirname);
  char *ptr = buf + strlen(dirname);
  *ptr++ = '/';

  /* recursive search */
  search(fd, buf, ptr, pattern);
  
  exit(0);
}
