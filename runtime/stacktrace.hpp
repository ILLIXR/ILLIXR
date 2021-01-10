#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

void handler(int sig) {
  void *array[1000];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 1000);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}

void set_stacktrace_handler() {
  signal(SIGSEGV, handler);   // install our handler
}
