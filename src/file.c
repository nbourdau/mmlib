/*
   @mindmaze_header@
*/
#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "mmlib.h"
#include "mmerrno.h"
#include "mmsysio.h"
#include "file-internal.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>

#ifdef _WIN32
#  include <io.h>
#  include <uchar.h>
#  include "utils-win32.h"
#else
#  include <unistd.h>
#  include <libgen.h>
#endif

#ifdef _WIN32
#  ifdef lseek
#    undef lseek
#  endif
#  define lseek         _lseeki64
#  define fsync         _commit

static
int ftruncate(int fd, mm_off_t size)
{
	errno_t ret;

	ret = _chsize_s(fd, size);
	if (ret != 0) {
		errno = ret;
		return -1;
	}

	return 0;
}

#endif //_WIN32


API_EXPORTED
int mm_fsync(int fd)
{
	if (fsync(fd) < 0)
		return mm_raise_from_errno("fsync(%i) failed", fd);

	return 0;
}


API_EXPORTED
mm_off_t mm_seek(int fd, mm_off_t offset, int whence)
{
	mm_off_t loc;

	loc = lseek(fd, offset, whence);
	if (loc < 0)
		return mm_raise_from_errno("lseek(%i, %lli, %i) failed", fd, offset, whence);

	return loc;
}


API_EXPORTED
int mm_ftruncate(int fd, mm_off_t length)
{
	if (ftruncate(fd, length) < 0)
		return mm_raise_from_errno("ftruncate(%i, %lli) failed", fd, length);

	return 0;
}


static
void conv_native_to_mm_stat(struct mm_stat* buf,
                            const struct stat* native_stat)
{
	buf->mode = native_stat->st_mode;
	buf->nlink = native_stat->st_nlink;
	buf->filesize = native_stat->st_size;
	buf->ctime = native_stat->st_ctime;
	buf->mtime = native_stat->st_mtime;
}


API_EXPORTED
int mm_fstat(int fd, struct mm_stat* buf)
{
	struct stat native_stat;

	if (fstat(fd, &native_stat) < 0)
		return mm_raise_from_errno("fstat(%i) failed", fd);

	conv_native_to_mm_stat(buf, &native_stat);
	return 0;
}


API_EXPORTED
int mm_stat(const char* path, struct mm_stat* buf)
{
	struct stat native_stat;

	if (stat(path, &native_stat) < 0)
		return mm_raise_from_errno("stat(%s) failed", path);

	conv_native_to_mm_stat(buf, &native_stat);
	return 0;
}

#if defined(_WIN32)
#define IS_PATH_SEPARATOR(c) \
	((*c) == '\\' || (*c) == '/')
#else
#define IS_PATH_SEPARATOR(c) \
	((*c) == '/')
#endif
/**
 * internal_dirname() -  quick implementation of dirname()
 * @path:         the path to get the dir of
 *
 * This MAY OR MAY NOT modify in-place the given path so as to transform path
 * to contain its dirname.
 *
 * Return: a pointer to path.
 * If no dirname can be extracted from the path, it will return "." instead.
 * Eg:
 * dirname("/usr/lib/") -> "/usr\0lib/"
 * dirname("usr") -> "."
 *
 * Note: windows dot not provide dirname, nor memrchr */
static
char * internal_dirname(char * path)
{
	char * c = path + strlen(path) - 1;

	/* skip the last chars if they're not a path */
	while (c > path && IS_PATH_SEPARATOR(c))
		c--;

	while (--c > path) {
		if (IS_PATH_SEPARATOR(c)) {
			/* remove consecutive separators (if any) */
			while (c > path && IS_PATH_SEPARATOR(c)) {
				*c = '\0';
				c--;
			}
			return path;
		}
	}
	return ".";
}

static
int internal_mkdir(const char* path, int mode)
{
#ifndef _WIN32
	return mkdir(path, mode);
#else
	int rv, path_u16_len;
	char16_t* path_u16;

	(void) mode;  // permission management is not supported on windows

	path_u16_len = get_utf16_buffer_len_from_utf8(path);
	if (path_u16_len < 0)
		return mm_raise_from_w32err("Invalid UTF-8 path");

	path_u16 = mm_malloca(path_u16_len * sizeof(*path_u16));
	if (path_u16 == NULL)
		return mm_raise_from_w32err("Failed to alloc required memory!");
	conv_utf8_to_utf16(path_u16, path_u16_len, path);

	rv = _wmkdir(path_u16);
	mm_freea(path_u16);
	return rv;
#endif
}

static
int mm_mkdir_rec(char* path, int mode)
{
	int rv;
	int len, len_orig;

	rv = internal_mkdir(path, mode);
	if (errno == EEXIST)
		return 0;
	else if (rv == 0 || errno != ENOENT)
		return rv;

	/* prevent recursion: dirname(".") == "." */
	if (is_wildcard_directory(path))
		return 0;

	len_orig = strlen(path);
	rv = mm_mkdir_rec(internal_dirname(path), mode);

	/* restore the dir separators removed by internal_dirname() */
	len = strlen(path);
	while (len < len_orig && path[len] == '\0')
		path[len++] = '/';

	if (rv != 0)
		return -1;

	return internal_mkdir(path, mode);
}

API_EXPORTED
int mm_mkdir(const char* path, int mode, int flags)
{
	int rv, len;
	char * tmp_path;

	rv = internal_mkdir(path, mode);

	if (flags & MM_RECURSIVE && rv != 0) {
		/* when recursive, do not raise an error when dir already present */
		if (errno == EEXIST)
			return 0;
		else if (errno != ENOENT)
			return mm_raise_from_errno("mkdir(%s) failed", path);


		len = strlen(path);
		tmp_path = mm_malloca(len + 1);
		strncpy(tmp_path, path, len + 1);
		rv = mm_mkdir_rec(tmp_path, 0777);
		mm_freea(tmp_path);
	}

	if (rv != 0)
		mm_raise_from_errno("mkdir(%s) failed", path);
	return rv;
}
