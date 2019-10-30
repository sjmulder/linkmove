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
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <err.h>

static void link_move(char *, char *);
static void link_move_multi(char **, int, char *);
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
 * Moves the file src to dst, leaving a symlink in to dst at src. dst
 * must not be a directory. The move is performed with a rename if src
 * and dst are on the same device, or by copying with copy() (see below)
 * if not.
 */
static void
link_move(char *src, char *dst)
{
	if (rename(src, dst) == -1) {
		if (errno != EXDEV)
			err(1, NULL);
		if (unlink(dst) == -1 && errno != ENOENT)
			err(1, "%s", dst);
		copy(src, dst);
		if (unlink(src) == -1)
			err(1, "%s", src);
	}
	if (symlink(dst, src) == -1)
		err(1, NULL);
}

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
 * Copies the file src to dst. dst must not be a directory. Dates,
 * ownership and permissions are copied insofar possible.
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
