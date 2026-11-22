// Reads mangled C++ names, one per line, and prints each demangled (a name that is not mangled is printed as it is), like `c++filt -n`.
// build.py compiles it with Zig (which brings libc++abi's demangler) where there is no c++filt, which is Windows: see tools/sysdeps.py.
#include <cxxabi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  static char line[1 << 16];
  while (fgets(line, sizeof line, stdin)) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    int status = 0;
    char *d = abi::__cxa_demangle(line, nullptr, nullptr, &status);
    fputs(status == 0 && d ? d : line, stdout);
    fputc('\n', stdout);
    free(d);
  }
  return 0;
}
