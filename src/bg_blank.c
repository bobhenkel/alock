/*
 * alock - bg_blank.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This projected is licensed under the terms of the MIT license.
 *
 * This background module provides:
 *  -bg blank:color=<color>
 *
 */

#include <stdlib.h>
#include <string.h>

#include "alock.h"


static Window *window = NULL;


static int alock_bg_blank_init(const char *args, struct aXInfo *xinfo) {

    if (!xinfo)
        return 0;

    XSetWindowAttributes xswa;
    XColor color;
    char *color_name = NULL;
    int scr;

    if (args && strstr(args, "blank:") == args) {
        char *arguments = strdup(&args[6]);
        char *arg;
        char *tmp;
        for (tmp = arguments; tmp; ) {
            arg = strsep(&tmp, ",");
            if (strstr(arg, "color=") == arg) {
                free(color_name);
                color_name = strdup(&arg[6]);
            }
        }
        free(arguments);
    }

    window = (Window*)malloc(sizeof(Window) * xinfo->screens);

    for (scr = 0; scr < xinfo->screens; scr++) {

        alock_alloc_color(xinfo, scr, color_name, "black", &color);

        xswa.override_redirect = True;
        xswa.colormap = xinfo->colormap[scr];
        xswa.background_pixel = color.pixel;

        window[scr] = XCreateWindow(xinfo->display, xinfo->root[scr],
                0, 0, xinfo->root_width[scr], xinfo->root_height[scr], 0,
                CopyFromParent, InputOutput, CopyFromParent,
                CWOverrideRedirect | CWColormap | CWBackPixel,
                &xswa);

        if (window[scr])
            xinfo->window[scr] = window[scr];

    }

    free(color_name); 
    return 1;
}

static int alock_bg_blank_deinit(struct aXInfo *xinfo) {

    if (!xinfo || !window)
        return 0;

    int scr;
    for (scr = 0; scr < xinfo->screens; scr++)
        XDestroyWindow(xinfo->display, window[scr]);
    free(window);

    return 1;
}


struct aBackground alock_bg_blank = {
    "blank",
    alock_bg_blank_init,
    alock_bg_blank_deinit,
};
