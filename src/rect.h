#ifndef RECT_H
#define RECT_H

struct rect {
	int x, y, w, h;
};

void rect_parse(struct rect *rect, struct rect rootrect, char *str);

#endif
