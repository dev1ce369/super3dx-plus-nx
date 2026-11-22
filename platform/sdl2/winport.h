/* What the Windows build needs of POSIX that mingw-w64 lacks, force-included in every file of it (build.py:
 * -include). The game's own code stays POSIX. */
#ifndef WMMT_WINPORT_H
#define WMMT_WINPORT_H

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef near /* (empty macros of the 16-bit days: the game has variables of these names) */
#undef far

/* the data files are binary: open() and fopen() without "b" must not translate line ends */
static inline void winport_binary_files(void) { _fmode = _O_BINARY; }

/* pread: a read at an offset that does not depend on, or move for the caller's purposes, the file position (the loading
 * thread and the game thread read the same files) */
static inline ssize_t pread(int fd, void *buf, size_t n, off_t off) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  if (h == INVALID_HANDLE_VALUE) return -1;
  OVERLAPPED ov;
  memset(&ov, 0, sizeof ov);
  ov.Offset = (DWORD)((unsigned long long)off & 0xffffffffu);
  ov.OffsetHigh = (DWORD)((unsigned long long)off >> 32);
  DWORD got = 0;
  if (!ReadFile(h, buf, (DWORD)n, &got, &ov)) return GetLastError() == ERROR_HANDLE_EOF ? 0 : -1;
  return (ssize_t)got;
}
#endif

#endif
