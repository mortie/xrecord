#include "rect.h"

#include "util.h"

void rect_parse(struct rect *rect, char *str) {
	int w = -1, h = -1, x = -1, y = -1;
	sscanf(str, "%ix%i+%i+%i", &w, &h, &x, &y);

	if (w >= 0) rect->w = w;
	if (h >= 0) rect->h = h;
	if (x >= 0) rect->x = x;
	if (y >= 0) rect->y = y;
}
