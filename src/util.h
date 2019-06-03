#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>

#define assume(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "%s:%i: Assumption failed: " #expr "\n", __FILE__, __LINE__); \
		abort(); \
	} \
} while (0)

#endif
