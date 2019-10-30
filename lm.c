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

static noreturn void usage(void);
static char *rebase(char *, char *);
static void link_move(char *, char *);
static void copy(char *, char *);
static struct timeval ts_to_tv(struct timespec);

int
main(int argc, char **argv)
{
	char *dst, *dst_full;
	struct stat dst_sb;
	int i, dst_isdir = 0;

	(void)argv;

	if (argc < 3)
		usage();

	dst = argv[argc-1];

	if (stat(dst, &dst_sb) != -1)
		dst_isdir = S_ISDIR(dst_sb.st_mode);
	else if (errno != ENOENT)
		err(1, "%s", dst);

	if (dst_isdir)
		for (i = 1; i < argc-1; i++) {
			dst_full = rebase(argv[i], argv[argc-1]);
			link_move(argv[i], dst_full);
			free(dst_full);
		}
	else if (argc > 3)
		errx(1, "%s: not a directory", dst);
	else
		link_move(argv[1], dst);

	return 0;
}

static noreturn void
usage(void)
{
	fprintf(stderr, "usage: lm <source> [...] <target>\n");
	exit(1);
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

static struct
timeval ts_to_tv(struct timespec ts)
{
	struct timeval tv;
	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t)(ts.tv_nsec / 1000);

	return tv;
}
