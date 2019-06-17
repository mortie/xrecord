#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define logfile stderr

#define logfmt(...) do { \
	fprintf(logfile, "%s:%i: ", __FILE__, __LINE__); \
	fprintf(logfile, __VA_ARGS__); \
} while (0)

#define logln(...) do { \
	logfmt(__VA_ARGS__); \
	fprintf(logfile, "\n"); \
} while (0)

#define logperror(...) do { \
	logfmt(__VA_ARGS__); \
	fprintf(logfile, ": %s\n", strerror(errno)); \
} while (0)

#define panic(...) do { \
	logln("*** PANIC: " __VA_ARGS__); \
	abort(); \
} while (0)

#define ppanic(...) do { \
	logfmt("*** PANIC: "); \
	fprintf(logfile, __VA_ARGS__); \
	fprintf(logfile, ": %s\n", strerror(errno)); \
} while (0)

#define assume(expr) do { \
	if (!(expr)) { \
		panic("Assumption failed: " #expr); \
		abort(); \
	} \
} while (0)

#define assume_unreached() \
	panic("Unreachable code reached.")

#endif
