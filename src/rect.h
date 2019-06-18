#ifndef RECT_H
#define RECT_H

struct rect {
	int x, y, w, h;
};

void rect_parse(struct rect *rect, char *str);

#endif
