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
#include <getopt.h>
#include <dirent.h>
#include <libgen.h>
#include <err.h>

static void move(char *, char *);
static void copy(char *, char *);
static void cleanup_path(char *);
static char *rebase_path(char *, char *);
static char *join_paths(char *, char *);
static struct timeval ts_to_tv(struct timespec);
static noreturn void usage(void);

static int verbose = 0;

int
main(int argc, char **argv)
{
	char *dst, *item_dst;
	struct stat dst_sb;
	int c, i, is_dir = 0;

#ifdef __OpenBSD__
	if (pledge("stdio rpath wpath cpath fattr chown", NULL) == -1)
		err(1, NULL);
#endif

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	dst = argv[argc-1];

	if (stat(dst, &dst_sb) != -1)
		is_dir = S_ISDIR(dst_sb.st_mode);
	else if (errno != ENOENT)
		err(1, "%s", dst);

	if (is_dir)
		for (i = 0; i < argc-1; i++) {
			cleanup_path(argv[i]);
			item_dst = rebase_path(argv[i], dst);
			move(argv[i], item_dst);
			if (symlink(item_dst, argv[i]) == -1)
				err(1, NULL);
			free(item_dst);
		}
	else if (argc > 2)
		errx(1, "%s: not a directory", dst);
	else {
		cleanup_path(argv[0]);
		move(argv[0], dst);
		if (symlink(dst, argv[0]) == -1)
			err(1, NULL);
	}
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

	if (verbose)
		fprintf(stderr, "%s\n", dst);

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
			ent_src = join_paths(src, ent->d_name);
			ent_dst = join_paths(dst, ent->d_name);
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
 * Remove trailing slash. Modifies path in-place.
 */
static void
cleanup_path(char *path)
{
	size_t len;

	len = strlen(path);
	while (len > 1 && path[len-1] == '/')
		path[--len] = '\0';
}

/*
 * Returns a newly allocated string with the filename portion of src
 * appended to dst_dir, separated by a /.
 */
static char *
rebase_path(char *src, char *dst_dir)
{
	char *src_copy, *src_name, *dst;

	if (!(src_copy = strdup(src)))
		err(1, NULL);
	if (!(src_name = basename(src_copy)))
		err(1, "%s", src);

	dst = join_paths(dst_dir, src_name);
	free(src_copy);

	return dst;
}

/*
 * Returns a newly allocated string containing the two arguments joined
 * by a slash.
 */
static char *
join_paths(char *left, char *right)
{
	size_t sz;
	int n;
	char *ret;

	sz = strlen(left) + strlen(right) + 2;
	if (!(ret = malloc(sz)))
		err(1, NULL);
	if ((n = snprintf(ret, sz, "%s/%s", left, right)) >= sz)
		errx(1, "snprintf overflow");

	return ret;
}

static struct timeval
ts_to_tv(struct timespec ts)
{
	struct timeval tv;
	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t)(ts.tv_nsec / 1000);

	return tv;
}

static noreturn void
usage(void)
{
	fprintf(stderr, "usage: linkmove [-v] source ... target\n");
	exit(1);
}
