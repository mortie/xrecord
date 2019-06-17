#ifndef TIMELINE_H
#define TIMELINE_H

/*
 * This timeline system just produces a file which logs some info.
 * It must be parsed by some tool to be useful.
 */

#include <stdio.h>

void timeline_init(FILE *f);
void timeline_register(char *name);
void timeline_begin(char *name);
void timeline_end(char *name);

#endif
