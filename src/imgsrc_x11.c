#include "imgsrc.h"

#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

#include "rect.h"
#include "util.h"

struct membuf_x11 {
	struct membuf membuf;
	XImage *image;
	XShmSegmentInfo shminfo;
};

struct imgsrc_x11 {
	struct imgsrc imgsrc;
	Display *display;
	Window root;
};

static struct membuf *alloc_membuf_x11(struct imgsrc *src_) {
	struct imgsrc_x11 *src = (struct imgsrc_x11 *)src_;
	struct membuf_x11 *membuf = malloc(sizeof(*membuf));

	// Create shm image
	membuf->image = XShmCreateImage(
			src->display, DefaultVisual(src->display, DefaultScreen(src->display)),
			32, ZPixmap, NULL, &membuf->shminfo, src->imgsrc.rect.w, src->imgsrc.rect.h);
	if (membuf->image == NULL)
		panic("XShmCreateImage failed");

	// Attach shm image
	int ret = membuf->shminfo.shmid = shmget(IPC_PRIVATE,
			membuf->image->bytes_per_line * membuf->image->height,
			IPC_CREAT|0777);
	if (ret < 0)
		ppanic("shmget");

	membuf->shminfo.shmaddr = membuf->image->data = shmat(membuf->shminfo.shmid, 0, 0);
	if (membuf->shminfo.shmaddr == (void *)-1)
		ppanic("shmat");

	membuf->shminfo.readOnly = False;
	if (!XShmAttach(src->display, &membuf->shminfo))
		panic("XShmAttach failed");

	src->imgsrc.bpl = membuf->image->bytes_per_line;
	membuf->membuf.data = membuf->image->data;

	return (struct membuf *)membuf;
}

static void init_x11(struct imgsrc *src_, struct rect rect) {
	struct imgsrc_x11 *src = (struct imgsrc_x11 *)src_;
	memcpy(&src->imgsrc.rect, &rect, sizeof(src->imgsrc.rect));
}

static void free_x11(struct imgsrc *src_) {
	struct imgsrc_x11 *src = (struct imgsrc_x11 *)src_;
	free(src);
}

static void get_frame_x11(struct imgsrc *src_, struct membuf *membuf_) {
	struct imgsrc_x11 *src = (struct imgsrc_x11 *)src_;
	struct membuf_x11 *membuf = (struct membuf_x11 *)membuf_;

	if (!XShmGetImage(
			src->display, src->root, membuf->image,
			src->imgsrc.rect.x, src->imgsrc.rect.y, AllPlanes))
		panic("XShmGetImage failed");
}

struct imgsrc *imgsrc_create_x11() {
	struct imgsrc_x11 *src = malloc(sizeof(*src));
	src->imgsrc.init = init_x11;
	src->imgsrc.free = free_x11;
	src->imgsrc.alloc_membuf = alloc_membuf_x11;
	src->imgsrc.get_frame = get_frame_x11;

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
