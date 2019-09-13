#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#define M_FULL_WRITE_PERMISSIONS 0733

/* make sure to use syserror() when a system call fails. see common.h */

void copy_file(char *src, char *dest) {
	int fd_src, fd_dest;
	struct stat sb;
	stat(src, &sb);
	
	fd_src = open(src, O_RDONLY);
	if (fd_src < 0)
		syserror(open, src);

	char buf[4096];
	int ret;

	fd_dest = creat(dest, M_FULL_WRITE_PERMISSIONS);
	if (fd_dest < 0)
		syserror(open, dest);

	while ((ret = read(fd_src, buf, 4096))) {
		if (ret < 0)
			syserror(read, src);
		if (write(fd_dest, buf, ret) < 0)
			syserror(write, dest);
	}

	if (chmod(dest, sb.st_mode) < 0)
		syserror(chmod, dest);

	close(fd_src);
	close(fd_dest);

}

void copy_dir(char *src, char *dest) {
	DIR *dir = opendir(src);
	struct stat sb;
	if (stat(src, &sb) < 0)
		syserror(stat, src);

	mode_t src_mode = sb.st_mode;

	if (mkdir(dest, M_FULL_WRITE_PERMISSIONS) < 0)
		syserror(mkdir, dest);

	struct dirent *dentry;
	while ((dentry = readdir(dir)) != NULL) {
		if (!strcmp(dentry->d_name, ".") || !strcmp(dentry->d_name, ".."))
			continue;

		char new_src[strlen(src) + strlen(dentry->d_name) + 2];
		strcpy(new_src, src);
		strcat(new_src, "/");
		strcat(new_src, dentry->d_name);

		char new_dest[strlen(dest) + strlen(dentry->d_name) + 2];
		strcpy(new_dest, dest);
		strcat(new_dest, "/");
		strcat(new_dest, dentry->d_name);


		if (stat(new_src, &sb) < 0)
			syserror(stat, new_src);

		if (S_ISDIR(sb.st_mode))
			copy_dir(new_src, new_dest);
		else if (S_ISREG(sb.st_mode))
			copy_file(new_src, new_dest);
	}

	if (chmod(dest, src_mode) < 0)
		syserror(chmod, dest);

	closedir(dir);
}

void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		usage();
	}
	char *src = argv[1];
	char *dest = argv[2];

	struct stat sb;
	int s = stat(src, &sb);

	if (s < 0)
		syserror(stat, src);
	else if (S_ISREG(sb.st_mode))
		copy_file(src, dest);
	else if (S_ISDIR(sb.st_mode))
		copy_dir(src, dest);

	return 0;
}
