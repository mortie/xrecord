#include "imgsrc.h"

#include <stdlib.h>
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

static void get_frame_x11(struct imgsrc_x11 *src, uint8_t *data) {
	XShmGetImage(
			src->display, src->root, src->image,
			src->imgsrc.rect.x, src->imgsrc.rect.y, AllPlanes);
}

struct imgsrc *imgsrc_create_x11(char *rectstr) {
	struct imgsrc_x11 *src = malloc(sizeof(*src));
	src->imgsrc.free = (void (*)(void *))free_x11;
	src->imgsrc.get_frame = (void (*)(void *, uint8_t *))get_frame_x11;

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
			24, ZPixmap, NULL, &shminfo, src->imgsrc.rect.w, src->imgsrc.rect.h);

	// Attach shm image
	shminfo.shmid = shmget(IPC_PRIVATE,
			src->image->bytes_per_line * src->image->height,
			IPC_CREAT|0777);
	shminfo.shmaddr = src->image->data = shmat(shminfo.shmid, 0, 0);
	shminfo.readOnly = False;
	XShmAttach(src->display, &shminfo);

	return (struct imgsrc *)src;
}
