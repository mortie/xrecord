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
	XShmSegmentInfo shminfo;
};

static void init_x11(struct imgsrc_x11 *src, struct rect rect) {
	memcpy(&src->imgsrc.rect, &rect, sizeof(src->imgsrc.rect));

	// Create shm image
	src->image = XShmCreateImage(
			src->display, DefaultVisual(src->display, DefaultScreen(src->display)),
			32, ZPixmap, NULL, &src->shminfo, src->imgsrc.rect.w, src->imgsrc.rect.h);
	if (src->image == NULL) {
		fprintf(stderr, "XShmCreateImage :(\n");
		exit(EXIT_FAILURE);
	}

	// Attach shm image
	int ret = src->shminfo.shmid = shmget(IPC_PRIVATE,
			src->image->bytes_per_line * src->image->height,
			IPC_CREAT|0777);
	if (ret < 0) {
		perror("shmget");
		exit(EXIT_FAILURE);
	}

	src->shminfo.shmaddr = src->image->data = shmat(src->shminfo.shmid, 0, 0);
	if (src->shminfo.shmaddr == (void *)-1) {
		perror("shmat");
		exit(EXIT_FAILURE);
	}

	src->shminfo.readOnly = False;
	if (!XShmAttach(src->display, &src->shminfo)) {
		fprintf(stderr, "XShmAttach :(\n");
		exit(EXIT_FAILURE);
	}
}

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

	fprintf(stderr, "got frame from x\n");

	return (struct imgbuf) {
		.data = src->image->data,
		.bpl = src->image->bytes_per_line,
	};
}

struct imgsrc *imgsrc_create_x11() {
	struct imgsrc_x11 *src = malloc(sizeof(*src));
	src->imgsrc.init = (void (*)(struct imgsrc *, struct rect))init_x11;
	src->imgsrc.free = (void (*)(struct imgsrc *))free_x11;
	src->imgsrc.get_frame = (struct imgbuf (*)(struct imgsrc *))get_frame_x11;

	src->display = XOpenDisplay(NULL);
	assume(src->display != NULL);

	src->root = XDefaultRootWindow(src->display);

	// Get screen size
	XWindowAttributes gwa;
	XGetWindowAttributes(src->display, src->root, &gwa);
	src->imgsrc.screensize.w = gwa.width;
	src->imgsrc.screensize.h = gwa.height;

	src->imgsrc.pixfmt = AV_PIX_FMT_BGRA;

	return (struct imgsrc *)src;
}
