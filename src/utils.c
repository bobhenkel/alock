/*
 * alock - utils.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This projected is licensed under the terms of the MIT license.
 *
 */

#if HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <X11/Xutil.h>
#if ENABLE_XRENDER
#include <X11/extensions/Xrender.h>
#endif

#include "alock.h"


/* Get system time-stamp in milliseconds without discontinuities. */
unsigned long alock_mtime() {
    struct timespec t;
    clock_gettime(CLOCK_BOOTTIME, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* Determine the Endianness of the system. */
int alock_native_byte_order() {
    int x = 1;
    return (*((char *) &x) == 1) ? LSBFirst : MSBFirst;
}

/* Allocate colormap entry by the given color name. When the color_name
 * parameter is NULL, then fallback value is used right away. */
int alock_alloc_color(Display *display,
        Colormap colormap,
        const char *color_name,
        const char *fallback_name,
        XColor *result) {

    if (!display || !colormap || !fallback_name || !result)
        return 0;

    XColor tmp;

    if (!color_name || XAllocNamedColor(display, colormap, color_name, &tmp, result) == 0)
        if (XAllocNamedColor(display, colormap, fallback_name, &tmp, result) == 0)
            return 0;
    return 1;
}

/* Check if the X server supports RENDER extension. This code was taken from
 * the cursor.c of libXcursor. */
int alock_check_xrender(Display *display) {
#if ENABLE_XRENDER
    static int have_xrender = 0;
    static int checked_already = 0;

    if (!checked_already) {
        int major_opcode, first_event, first_error;
        if (XQueryExtension(display, "RENDER",
                            &major_opcode,
                            &first_event, &first_error) == False) {
            fprintf(stderr, "alock: no xrender-support found\n");
            have_xrender = 0;
        }
        else
            have_xrender = 1;

        checked_already = 1;
    }
    return have_xrender;
#else
    (void)display;
    fprintf(stderr, "alock: i wasnt compiled to support xrender\n");
    return 0;
#endif /* ENABLE_XRENDER */
}

/* Shade given source pixmap by the amount specified by the shade parameter,
 * which should be in range [0, 100]. */
int alock_shade_pixmap(Display *display,
        Visual *visual,
        const Pixmap src_pm,
        Pixmap dst_pm,
        unsigned char shade,
        int src_x, int src_y,
        int dst_x, int dst_y,
        unsigned int width,
        unsigned int height) {
#if ENABLE_XRENDER
    Picture alpha_pic = None;
    XRenderPictFormat *format;

    {
        XRenderPictFormat alpha_format;
        alpha_format.type = PictTypeDirect;
        alpha_format.depth = 8;
        alpha_format.direct.alpha = 0;
        alpha_format.direct.alphaMask = 0xff;

        format = XRenderFindFormat(display,
              PictFormatType | PictFormatDepth | PictFormatAlpha | PictFormatAlphaMask,
              &alpha_format, 0);
    }

    if (!format) {
        fprintf(stderr, "alock: couldn't find valid format for alpha\n");
        XFreePixmap(display, dst_pm);
        XFreePixmap(display, src_pm);
        return 0;
    }

    { /* fill the alpha-picture */
        Pixmap pm;
        XRenderColor color;
        XRenderPictureAttributes pa;

        if (shade > 100)
            shade = 100;

        pa.repeat = True;
        color.alpha = 0xffff * shade / 100;

        pm = XCreatePixmap(display, src_pm, 1, 1, 8);
        alpha_pic = XRenderCreatePicture(display, pm, format, CPRepeat, &pa);
        XRenderFillRectangle(display, PictOpSrc, alpha_pic, &color, 0, 0, 1, 1);

        XFreePixmap(display, pm);
    }

    { /* blend all together */
        Picture src_pic;
        Picture dst_pic;

        format = XRenderFindVisualFormat(display, visual);
        src_pic = XRenderCreatePicture(display, src_pm, format, 0, 0);
        dst_pic = XRenderCreatePicture(display, dst_pm, format, 0, 0);

        XRenderComposite(display, PictOpOver,
                         src_pic, alpha_pic, dst_pic,
                         src_x, src_y, 0, 0, dst_x, dst_y, width, height);
        XRenderFreePicture(display, src_pic);
        XRenderFreePicture(display, dst_pic);
    }

    XRenderFreePicture(display, alpha_pic);
    return 1;
#else
    (void)display;
    (void)visual;
    return 0;
#endif /* ENABLE_XRENDER */
}

/* Blur given source pixmap using a Gaussian convolution filter. Whole
 * operation is performed by the X server and when possible hardware
 * accelerated. For the best results the blur parameter should be in the
 * range [0, 100]. */
int alock_blur_pixmap(Display *display,
        Visual *visual,
        const Pixmap src_pm,
        Pixmap dst_pm,
        unsigned char blur,
        int src_x, int src_y,
        int dst_x, int dst_y,
        unsigned int width,
        unsigned int height) {
#if ENABLE_XRENDER
    if (!blur)
        /* TODO: copy source pixmap to the destination one */
        return 1;

    int size = (blur / 20) * 2 + 5;
    XFixed *params = malloc(sizeof(XFixed) * (size + 2));

    { /* calculate sampled Gaussian kernel */
        double *kernel = malloc(sizeof(double) * size);
        double sigma = size / 3.0;
        double denom = 2 * sigma * sigma;
        double scale = sqrt(M_PI * denom);
        double vsum = 0.0;
        int i;

        for (i = 0; i < size; i++) {
            int n = i - size / 2;
            kernel[i] = exp(-(n * n) / denom) / scale;
            vsum += kernel[i];
        }
        for (i = 0; i < size; i++)
            params[i + 2] = XDoubleToFixed(kernel[i] / vsum);

        free(kernel);
    }

    { /* 2D blur using convolution filter */
        XRenderPictFormat *format;
        Picture src_pic;
        Picture dst_pic;
        Picture tmp_pic;

        /* TODO: find a better way to make 2D Gaussian blur */

        format = XRenderFindVisualFormat(display, visual);
        src_pic = XRenderCreatePicture(display, src_pm, format, 0, NULL);
        dst_pic = XRenderCreatePicture(display, dst_pm, format, 0, NULL);

        params[0] = XDoubleToFixed(size);
        params[1] = XDoubleToFixed(1);
        XRenderSetPictureFilter(display, src_pic, FilterConvolution, params, size + 2);
        XRenderComposite(display, PictOpSrc, src_pic, None, dst_pic,
                src_x, src_y, 0, 0, dst_x, dst_y, width, height);

        params[0] = XDoubleToFixed(1);
        params[1] = XDoubleToFixed(size);
        XRenderSetPictureFilter(display, dst_pic, FilterConvolution, params, size + 2);
        XRenderComposite(display, PictOpOver, dst_pic, None, dst_pic,
                src_x, src_y, 0, 0, dst_x, dst_y, width, height);

        XRenderFreePicture(display, src_pic);
        XRenderFreePicture(display, dst_pic);
    }

    free(params);
    return 1;
#else
    (void)display;
    (void)visual;
    return 0;
#endif /* ENABLE_XRENDER */
}

/* Convert given color image to the grayscale intensity one. Note, that this
 * function performs in-place conversion. */
int alock_grayscale_image(XImage *image,
        int x, int y,
        unsigned int width,
        unsigned int height) {

    union {
        struct {
            unsigned char red;
            unsigned char green;
            unsigned char blue;
            unsigned char alpha;
        } v;
        unsigned long value;
    } pixel;
    int _x, _y;

    for (_x = x; _x < width; _x++)
        for (_y = y; _y < height; _y++) {
            pixel.value = XGetPixel(image, _x, _y);
            pixel.v.red = pixel.v.green = pixel.v.blue = (
                    pixel.v.red + pixel.v.green + pixel.v.blue) / 3;
            XPutPixel(image, _x, _y, pixel.value);
        }

    return 1;
}

/* Dummy function for module interface. */
void module_dummy_loadargs(const char *args) {
    (void)args;
    debug("dummy loadargs: %s", args);
}

/* Dummy function for module interface. */
void module_dummy_loadxrdb(XrmDatabase database) {
    (void)database;
    debug("dummy loadxrdb");
}

/* Dummy function for module interface. */
int module_dummy_init(struct aDisplayInfo *dinfo) {
    (void)dinfo;
    debug("dummy init");
    return 0;
}

/* Dummy function for module interface. */
void module_dummy_free(void) {
    debug("dummy free");
}
