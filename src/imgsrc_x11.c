#include "imgsrc.h"

#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

#include "rect.h"
#include "util.h"

struct imgsrc_x11 {
	struct imgsrc imgsrc;
	Display *display;
	Window root;
	XImage *image;
};

static void free_x11(struct imgsrc_x11 *src) {
	free(src);
}

static struct imgbuf get_frame_x11(struct imgsrc_x11 *src) {
	if (!XShmGetImage(
			src->display, src->root, src->image,
			src->imgsrc.rect.x, src->imgsrc.rect.y, AllPlanes)) {
		fprintf(stderr, "XShmGetImage :(\n");
		exit(EXIT_FAILURE);
	}

	return (struct imgbuf) {
		.fmt = AV_PIX_FMT_BGRA,
		.data = src->image->data,
		.bpl = src->image->bytes_per_line,
	};
}

struct imgsrc *imgsrc_create_x11(char *rectstr) {
	struct imgsrc_x11 *src = malloc(sizeof(*src));
	src->imgsrc.free = (void (*)(struct imgsrc *))free_x11;
	src->imgsrc.get_frame = (struct imgbuf (*)(struct imgsrc *))get_frame_x11;

	src->display = XOpenDisplay(NULL);
	assume(src->display != NULL);

	src->root = XDefaultRootWindow(src->display);

	XWindowAttributes gwa;
	XGetWindowAttributes(src->display, src->root, &gwa);

	rect_parse(&src->imgsrc.rect, (struct rect) { 0, 0, gwa.width, gwa.height }, rectstr);

	// Create shm image
	XShmSegmentInfo shminfo;
	src->image = XShmCreateImage(
			src->display, DefaultVisual(src->display, DefaultScreen(src->display)),
			32, ZPixmap, NULL, &shminfo, src->imgsrc.rect.w, src->imgsrc.rect.h);
	if (src->image == NULL) {
		fprintf(stderr, "XShmCreateImage :(\n");
		exit(EXIT_FAILURE);
	}

	// Attach shm image
	int ret = shminfo.shmid = shmget(IPC_PRIVATE,
			src->image->bytes_per_line * src->image->height,
			IPC_CREAT|0777);
	if (ret < 0) {
		perror("shmget");
		exit(EXIT_FAILURE);
	}

	shminfo.shmaddr = src->image->data = shmat(shminfo.shmid, 0, 0);
	if (shminfo.shmaddr == (void *)-1) {
		perror("shmat");
		exit(EXIT_FAILURE);
	}

	shminfo.readOnly = False;
	if (!XShmAttach(src->display, &shminfo)) {
		fprintf(stderr, "XShmAttach :(\n");
		exit(EXIT_FAILURE);
	}

	return (struct imgsrc *)src;
}
