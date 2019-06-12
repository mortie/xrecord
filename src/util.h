#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define logfmt(...) do { \
	fprintf(stderr, "%s:%i: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
} while (0)

#define logln(...) do { \
	logfmt(__VA_ARGS__); \
	fprintf(stderr, "\n"); \
} while (0)

#define logperror(...) do { \
	logfmt(__VA_ARGS__); \
	fprintf(stderr, ": %s\n", strerror(errno)); \
} while (0)

#define panic(...) do { \
	logln("*** PANIC: " __VA_ARGS__); \
	exit(EXIT_FAILURE); \
} while (0)

#define assume(expr) do { \
	if (!(expr)) { \
		panic("Assumption failed: " #expr); \
		abort(); \
	} \
} while (0)

#endif
