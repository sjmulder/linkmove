#ifdef _GNU
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <err.h>

static void link_move(char *, char *);
static void link_move_multi(char **, int, char *);
static void move(char *, char *);
static void copy(char *, char *);
static char *rebase(char *, char *);
static struct timeval ts_to_tv(struct timespec);
static noreturn void usage(void);

int
main(int argc, char **argv)
{
	if (argc < 3)
		usage();

	link_move_multi(&argv[1], argc-2, argv[argc-1]);

	return 0;
}

/*
 * Moves the file src to dst, leaving a symlink in to dst at src. The
 * move is performed with a rename if src and dst are on the same
 * device, or by copying with copy() (see below) if not.
 *
 * src may be a file or directory path.
 * dst must be the target path, not the parent directory.
 */
static void
link_move(char *src, char *dst)
{
	move(src, dst);

	if (symlink(dst, src) == -1)
		err(1, NULL);
}

/*
 * Perform link_move() on a list of source files.
 *
 * src may be file or directory paths.
 * dst may be a file or parent directory path.
 */
static void
link_move_multi(char **srcs, int count, char *dst)
{
	char *dst_full;
	struct stat dst_sb;
	int i, is_dir = 0;

	if (stat(dst, &dst_sb) != -1)
		is_dir = S_ISDIR(dst_sb.st_mode);
	else if (errno != ENOENT)
		err(1, "%s", dst);

	if (is_dir)
		for (i = 0; i < count; i++) {
			dst_full = rebase(srcs[i], dst);
			link_move(srcs[i], dst_full);
			free(dst_full);
		}
	else if (count > 1)
		errx(1, "%s: not a directory", dst);
	else
		link_move(srcs[0], dst);
}

/*
 * Rename src to dst, or copy() and delete if on different filesystems.
 *
 * src may be a file or directory path.
 * dst must be the full new path, not just the parent directory
 */
static void
move(char *src, char *dst)
{
	DIR *dir;
	struct dirent *ent;
	struct stat src_sb;
	char *ent_src, *ent_dst;

	if (rename(src, dst) != -1)
		; /* good, done */
	else if (errno != EXDEV)
		err(1, NULL);
	else if ((dir = opendir(src))) {
		if (stat(src, &src_sb) == -1)
			err(1, "%s", src);
		if (mkdir(dst, src_sb.st_mode) == -1)
			err(1, "%s", dst);
		if (chown(dst, src_sb.st_uid, src_sb.st_gid) == -1)
			warn("%s", dst);
		while ((ent = readdir(dir))) {
			if (strcmp(ent->d_name, ".") == 0)
				continue;
			if (strcmp(ent->d_name, "..") == 0)
				continue;
			asprintf(&ent_src, "%s/%s", src, ent->d_name);
			if (!ent_src)
				err(1, NULL);
			asprintf(&ent_dst, "%s/%s", dst, ent->d_name);
			if (!ent_dst)
				err(1, NULL);
			move(ent_src, ent_dst);
			free(ent_dst);
			free(ent_src);
		}
		closedir(dir);
		if (rmdir(src) == -1)
			err(1, "%s", dst);
	} else if (errno != ENOTDIR)
		err(1, "%s", src);
	else {
		copy(src, dst);
		if (unlink(src) == -1)
			err(1, "%s", src);
	}
}

/*
 * Copies the file src to dst. dst must not be a directory. Dates,
 * ownership and permissions are copied insofar possible.
 *
 * src must be a file path.
 * dst must be a file path.
 */
static void
copy(char *src, char *dst)
{
	static char buf[4096];
	struct stat src_sb;
	struct timeval times[2];
	int src_fd, dst_fd;
	ssize_t nread, nwritten;

	if (stat(src, &src_sb) == -1)
		err(1, "%s", dst);
	if ((src_fd = open(src, O_RDONLY | O_NOFOLLOW)) == -1)
		err(1, "%s", src);

	dst_fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, src_sb.st_mode);
	if (dst_fd == -1)
		err(1, "%s", dst);

	while ((nread = read(src_fd, buf, sizeof(buf)))) {
		if (nread == -1)
			err(1, "%s", src);
		if ((nwritten = write(dst_fd, buf, nread)) != nread)
			err(1, "%s", dst);
	}

#ifdef __APPLE__
	times[0] = ts_to_tv(src_sb.st_atimespec);
	times[1] = ts_to_tv(src_sb.st_ctimespec);
#else
	times[0] = ts_to_tv(src_sb.st_atim);
	times[1] = ts_to_tv(src_sb.st_ctim);
#endif

	if (futimes(dst_fd, times) == -1)
		warn("%s", dst);
	if (fchown(dst_fd, src_sb.st_uid, src_sb.st_gid) == -1)
		warn("%s", dst);

	if (close(dst_fd) == -1)
		err(1, "%s", dst);
	if (close(src_fd) == -1)
		err(1, "%s", src);
}

/*
 * Returns a newly allocated string with the filename portion of src
 * appended to dst_dir, separated by a /.
 */
static char *
rebase(char *src, char *dst_dir)
{
	char *src_copy, *src_name, *dst;

	if (!(src_copy = strdup(src)))
		err(1, NULL);
	if (!(src_name = basename(src_copy)))
		err(1, "%s", src);
	if (asprintf(&dst, "%s/%s", dst_dir, src_name) == -1)
		err(1, NULL);

	free(src_copy);

	return dst;
}

static struct
timeval ts_to_tv(struct timespec ts)
{
	struct timeval tv;
	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t)(ts.tv_nsec / 1000);

	return tv;
}

static noreturn void
usage(void)
{
	fprintf(stderr, "usage: lm <source> [...] <target>\n");
	exit(1);
}
