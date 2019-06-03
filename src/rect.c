#include "rect.h"

void rect_parse(struct rect *rect, struct rect rootrect, char *str) {
	rect->x = rootrect.x;
	rect->y = rootrect.y;
	rect->w = rootrect.w;
	rect->h = rootrect.h;
}
