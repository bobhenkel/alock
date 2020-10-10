/*
 * alock - alock.c
 * Copyright (c) 2005 - 2007 Mathias Gumz <akira at fluxbox dot org>
 *               2014 Arkadiusz Bokowy
 *
 * This file is a part of an alock.
 *
 * This projected is licensed under the terms of the MIT license.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xos.h>

#ifdef HAVE_XF86MISC
#include <X11/extensions/xf86misc.h>
#endif

#include "alock.h"


extern struct aAuth alock_auth_none;
#ifdef HAVE_HASH
extern struct aAuth alock_auth_md5;
extern struct aAuth alock_auth_sha1;
extern struct aAuth alock_auth_sha256;
extern struct aAuth alock_auth_sha384;
extern struct aAuth alock_auth_sha512;
extern struct aAuth alock_auth_wpool;
#endif /* HAVE_HASH */
#ifdef HAVE_PASSWD
extern struct aAuth alock_auth_passwd;
#endif /* HAVE_PASSWD */
#ifdef HAVE_PAM
extern struct aAuth alock_auth_pam;
#endif /* HAVE_PAM */
static struct aAuth* alock_authmodules[] = {
#ifdef HAVE_PAM
    &alock_auth_pam,
#endif /* HAVE_PAM */
#ifdef HAVE_PASSWD
    &alock_auth_passwd,
#endif /* HAVE_PASSWD */
#ifdef HAVE_HASH
    &alock_auth_md5,
    &alock_auth_sha1,
    &alock_auth_sha256,
    &alock_auth_sha384,
    &alock_auth_sha512,
    &alock_auth_wpool,
#endif /* HAVE_HASH */
    &alock_auth_none,
    NULL
};

extern struct aInput alock_input_none;
extern struct aInput alock_input_frame;
static struct aInput* alock_inputs[] = {
    &alock_input_frame,
    &alock_input_none,
    NULL
};

extern struct aBackground alock_bg_none;
extern struct aBackground alock_bg_blank;
#ifdef HAVE_IMLIB2
extern struct aBackground alock_bg_image;
#endif /* HAVE_IMLIB2 */
#ifdef HAVE_XRENDER
extern struct aBackground alock_bg_shade;
#endif /* HAVE_XRENDER */
static struct aBackground* alock_backgrounds[] = {
    &alock_bg_blank,
#ifdef HAVE_IMLIB2
    &alock_bg_image,
#endif /* HAVE_IMLIB2 */
#ifdef HAVE_XRENDER
    &alock_bg_shade,
#endif /* HAVE_XRENDER */
    &alock_bg_none,
    NULL
};

extern struct aCursor alock_cursor_none;
extern struct aCursor alock_cursor_glyph;
extern struct aCursor alock_cursor_theme;
#ifdef HAVE_XCURSOR
extern struct aCursor alock_cursor_xcursor;
#if (defined(HAVE_XRENDER) && (defined(HAVE_XPM) || (defined(HAVE_IMLIB2))))
extern struct aCursor alock_cursor_image;
#endif /* HAVE_XRENDER && (HAVE_XPM || HAVE_IMLIB2) */
#endif /* HAVE_XCURSOR */
static struct aCursor* alock_cursors[] = {
    &alock_cursor_none,
    &alock_cursor_theme,
    &alock_cursor_glyph,
#ifdef HAVE_XCURSOR
    &alock_cursor_xcursor,
#if (defined(HAVE_XRENDER) && ((defined(HAVE_XPM) || defined(HAVE_IMLIB2))))
    &alock_cursor_image,
#endif /* HAVE_XRENDER && (HAVE_XPM || HAVE_IMLIB2) */
#endif /* HAVE_XCURSOR */
    NULL
};


static void initXInfo(struct aXInfo* xi) {

    Display* dpy = XOpenDisplay(NULL);

    if (!dpy) {
        perror("alock: error, can't open connection to X");
        exit(EXIT_FAILURE);
    }

    {
        xi->display = dpy;
        xi->pid_atom = XInternAtom(dpy, "_ALOCK_PID", False);
        xi->nr_screens = ScreenCount(dpy);
        xi->window = (Window*)calloc((size_t)xi->nr_screens, sizeof(Window));
        xi->root = (Window*)calloc((size_t)xi->nr_screens, sizeof(Window));
        xi->colormap = (Colormap*)calloc((size_t)xi->nr_screens, sizeof(Colormap));
        xi->cursor = (Cursor*)calloc((size_t)xi->nr_screens, sizeof(Cursor));
        xi->width_of_root = (int*)calloc(xi->nr_screens, sizeof(int));
        xi->height_of_root = (int*)calloc(xi->nr_screens, sizeof(int));
    }
    {
        XWindowAttributes xgwa;
        int scr;
        for (scr = 0; scr < xi->nr_screens; scr++) {
            xi->window[scr] = None;
            xi->root[scr] = RootWindow(dpy, scr);
            xi->colormap[scr] = DefaultColormap(dpy, scr);

            XGetWindowAttributes(dpy, xi->root[scr], &xgwa);
            xi->width_of_root[scr] = xgwa.width;
            xi->height_of_root[scr] = xgwa.height;
        }
    }
}

static void eventLoop(struct aOpts* opts, struct aXInfo* xi) {

    Display* dpy = xi->display;
    XEvent ev;
    KeySym ks;
    char cbuf[10];
    char pass[128];
    unsigned int clen, rlen = 0;
    unsigned long keypress_time = 0;

    debug("entering event main loop");
    for(;;) {

        if (keypress_time) {
            /* check for any key press event */
            if (XCheckWindowEvent(dpy, xi->window[0], KeyPressMask | KeyReleaseMask, &ev) == False) {

                /* user fell asleep while typing (5 seconds inactivity) */
                if (alock_mtime() - keypress_time > 5000) {
                    opts->input->setstate(AINPUT_STATE_NONE);
                    keypress_time = 0;
                }

                /* wait a bit */
                usleep(25000);
                continue;
            }
        } else {
            /* block until any key press event arrives */
            XWindowEvent(dpy, xi->window[0], KeyPressMask | KeyReleaseMask, &ev);
        }

        switch (ev.type) {
        case KeyPress:

            /* swallow up first key press to indicate "enter mode" */
            if (keypress_time == 0) {
                opts->input->setstate(AINPUT_STATE_INIT);
                keypress_time = alock_mtime();
                break;
            }

            /* TODO: utf8 support */
            keypress_time = alock_mtime();
            clen = XLookupString(&ev.xkey, cbuf, 9, &ks, 0);
            switch (ks) {
            case XK_Escape:
            case XK_Clear:
                rlen = 0;
                break;
            case XK_Delete:
            case XK_BackSpace:
                if (rlen > 0)
                    rlen--;
                break;
            case XK_Linefeed:
            case XK_Return:
                pass[rlen] = 0;
                opts->input->setstate(AINPUT_STATE_CHECK);
                if (opts->auth->auth(pass)) {
                    opts->input->setstate(AINPUT_STATE_VALID);
                    return;
                }
                opts->input->setstate(AINPUT_STATE_ERROR);
                opts->input->setstate(AINPUT_STATE_INIT);
                rlen = 0;
                break;
            default:
                if (clen != 1)
                    break;
                if (rlen < sizeof(pass) - 1) {
                    opts->input->keypress('*');
                    pass[rlen++] = cbuf[0];
                }
                break;
            }
            debug("entered phrase: `%s`", pass);
            break;

        case Expose:
            XClearWindow(xi->display, ((XExposeEvent*)&ev)->window);
            break;
        }
    }
}

static pid_t getPidAtom(struct aXInfo* xinfo) {

    Atom ret_type;
    int ret_fmt;
    unsigned long nr_read;
    unsigned long nr_bytes_left;
    pid_t* ret_data;

    if (XGetWindowProperty(xinfo->display, xinfo->root[0],
                xinfo->pid_atom, 0L, 1L, False, XA_CARDINAL,
                &ret_type, &ret_fmt, &nr_read, &nr_bytes_left,
                (unsigned char**)&ret_data) == Success && ret_type != None && ret_data) {
        pid_t pid = *ret_data;
        XFree(ret_data);
        return pid;
    }

    return 0;
}

static int detectOtherInstance(struct aXInfo* xinfo) {

    pid_t pid = getPidAtom(xinfo);
    int process_alive = kill(pid, 0);

    if (pid > 0 && process_alive == 0) {
        return 1;
    }

    if (process_alive) {
        perror("alock: info, found _ALOCK_PID");
    }

    return 0;
}

static int registerInstance(struct aXInfo* xinfo) {

    pid_t pid = getpid();
    XChangeProperty(xinfo->display, xinfo->root[0],
            xinfo->pid_atom, XA_CARDINAL,
            sizeof(pid_t) * 8, PropModeReplace,
            (unsigned char*)&pid, 1);
    return 1;
}

static int unregisterInstance(struct aXInfo* xinfo) {

    XDeleteProperty(xinfo->display, xinfo->root[0], xinfo->pid_atom);
    return 1;
}

int main(int argc, char **argv) {

    struct aXInfo xinfo;
    struct aOpts opts;

#if HAVE_XF86MISC
    int xf86misc_major = -1;
    int xf86misc_minor = -1;
#endif

    int arg;
    const char* optarg;
    const char* auth_args = NULL;
    const char* input_args = NULL;
    const char* cursor_args = NULL;
    const char* background_args = "blank:color=black";

    opts.auth = alock_authmodules[0];
    opts.input = alock_inputs[0];
    opts.cursor = alock_cursors[0];
    opts.background = alock_backgrounds[0];

    /* parse options */
    if (argc > 1) {
        for (arg = 1; arg < argc; arg++) {
            if (!strcmp(argv[arg], "-bg")) {
                optarg = argv[++arg];
                if (optarg != NULL) {

                    struct aBackground** i;
                    if (strcmp(optarg, "list") == 0) {
                        for (i = alock_backgrounds; *i; ++i) {
                            printf("%s\n", (*i)->name);
                        }
                        exit(EXIT_SUCCESS);
                    }

                    for (i = alock_backgrounds; *i; ++i) {
                        if(strstr(optarg, (*i)->name) == optarg) {
                            background_args = optarg;
                            opts.background = *i;
                            break;
                        }
                    }

                    if (*i == NULL) {
                        fprintf(stderr, "alock: couldnt find the bg-module you specified\n");
                        exit(EXIT_FAILURE);
                    }

                } else {
                    fprintf(stderr, "alock: missing argument\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[arg], "-auth")) {
                optarg = argv[++arg];
                if (optarg != NULL) {

                    struct aAuth** i;
                    if (strcmp(optarg, "list") == 0) {
                        for (i = alock_authmodules; *i; ++i) {
                            printf("%s\n", (*i)->name);
                        }
                        exit(EXIT_SUCCESS);
                    }

                    for (i = alock_authmodules; *i; ++i) {
                        if(strstr(optarg, (*i)->name) == optarg) {
                            auth_args = optarg;
                            opts.auth = *i;
                            break;
                        }
                    }

                    if (*i == NULL) {
                        fprintf(stderr, "alock: couldnt find the auth-module you specified\n");
                        exit(EXIT_FAILURE);
                    }

                } else {
                    fprintf(stderr, "alock: missing argument\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[arg], "-cursor")) {
                optarg = argv[++arg];
                if (optarg != NULL) {

                    struct aCursor** i;
                    if (strcmp(argv[arg], "list") == 0) {
                        for (i = alock_cursors; *i; ++i) {
                            printf("%s\n", (*i)->name);
                        }
                        exit(EXIT_SUCCESS);
                    }

                    for (i = alock_cursors; *i; ++i) {
                        if(strstr(optarg, (*i)->name) == optarg) {
                            cursor_args = optarg;
                            opts.cursor = *i;
                            break;
                        }
                    }

                    if (*i == NULL) {
                        fprintf(stderr, "alock: couldnt find the cursor-module you specified\n");
                        exit(EXIT_FAILURE);
                    }

                } else {
                    fprintf(stderr, "alock: missing argument\n");
                    exit(EXIT_FAILURE);
                }
            } else if (!strcmp(argv[arg], "-input")) {
                optarg = argv[++arg];
                if (optarg != NULL) {

                    struct aInput** i;
                    if (strcmp(argv[arg], "list") == 0) {
                        for (i = alock_inputs; *i; ++i) {
                            printf("%s\n", (*i)->name);
                        }
                        exit(EXIT_SUCCESS);
                    }

                    for (i = alock_inputs; *i; ++i) {
                        if(strstr(optarg, (*i)->name) == optarg) {
                            input_args = optarg;
                            opts.input = *i;
                            break;
                        }
                    }

                    if (*i == NULL) {
                        fprintf(stderr, "alock: couldnt find the input-module you specified\n");
                        exit(EXIT_FAILURE);
                    }

                } else {
                    fprintf(stderr, "alock: missing argument\n");
                    exit(EXIT_FAILURE);
                }
            } else if (strcmp(argv[arg], "-h") == 0) {
                printf("alock [-h] [-bg type:options] [-cursor type:options] "
                       "[-auth type:options] [-input type:options]\n");
                exit(EXIT_SUCCESS);
            } else {
                fprintf(stderr, "alock: invalid option '%s'\n", argv[arg]);
                exit(EXIT_FAILURE);
            }
        }
    }

    initXInfo(&xinfo);
    if (detectOtherInstance(&xinfo)) {
        fprintf(stderr, "alock: another instance seems to be running\n");
        exit(EXIT_FAILURE);
    }

    if (opts.auth->init(auth_args) == 0) {
        fprintf(stderr, "alock: failed init of [%s] with [%s]\n",
                opts.auth->name,
                auth_args);
        exit(EXIT_FAILURE);
    }

    if (opts.input->init(input_args, &xinfo) == 0) {
        fprintf(stderr, "alock: failed init of [%s] with [%s]\n",
                opts.input->name,
                input_args);
        exit(EXIT_FAILURE);
    }

    if (opts.background->init(background_args, &xinfo) == 0) {
        fprintf(stderr, "alock: failed init of [%s] with [%s]\n",
                opts.background->name,
                background_args);
        exit(EXIT_FAILURE);
    }

    if (opts.cursor->init(cursor_args, &xinfo) == 0) {
        fprintf(stderr, "alock: failed init of [%s] with [%s]\n",
                opts.cursor->name,
                cursor_args);
        exit(EXIT_FAILURE);
    }

    {
        int scr;
        for (scr = 0; scr < xinfo.nr_screens; scr++) {

            XSelectInput(xinfo.display, xinfo.window[scr], KeyPressMask|KeyReleaseMask);
            XMapWindow(xinfo.display, xinfo.window[scr]);
            XRaiseWindow(xinfo.display, xinfo.window[scr]);

        }
    }

    /* try to grab 2 times, another process (windowmanager) may have grabbed
     * the keyboard already */
    if ((XGrabKeyboard(xinfo.display, xinfo.window[0], True, GrabModeAsync, GrabModeAsync,
                          CurrentTime)) != GrabSuccess) {
        sleep(1);
        if ((XGrabKeyboard(xinfo.display, xinfo.window[0], True, GrabModeAsync, GrabModeAsync,
                        CurrentTime)) != GrabSuccess) {
            printf("alock: couldnt grab the keyboard\n");
            exit(EXIT_FAILURE);
        }
    }

#if HAVE_XF86MISC
    {
        if (XF86MiscQueryVersion(xinfo.display, &xf86misc_major, &xf86misc_minor) == True) {

            if (xf86misc_major >= 0 &&
                xf86misc_minor >= 5 &&
                XF86MiscSetGrabKeysState(xinfo.display, False) == MiscExtGrabStateLocked) {

                fprintf(stderr, "alock: cant disable xserver hotkeys to remove grabs\n");
                exit(EXIT_FAILURE);
            }

            printf("disabled AllowDeactivateGrabs and AllowClosedownGrabs\n");
        }
    }
#endif

    /* TODO: think about it: do we really need NR_SCREEN cursors ? we grab the
     * pointer on :*.0 anyway ... */
    if (XGrabPointer(xinfo.display, xinfo.window[0], False, None,
                     GrabModeAsync, GrabModeAsync, None, xinfo.cursor[0], CurrentTime) != GrabSuccess) {
        XUngrabKeyboard(xinfo.display, CurrentTime);
        fprintf(stderr, "alock: couldnt grab the pointer\n");
        exit(EXIT_FAILURE);
    }

    registerInstance(&xinfo);
    eventLoop(&opts, &xinfo);
    unregisterInstance(&xinfo);

    opts.auth->deinit();
    opts.input->deinit(&xinfo);
    opts.cursor->deinit(&xinfo);
    opts.background->deinit(&xinfo);

#if HAVE_XF86MISC
    if (xf86misc_major >= 0 && xf86misc_minor >= 5) {
        XF86MiscSetGrabKeysState(xinfo.display, True);
        XFlush(xinfo.display);
    }
#endif

    XCloseDisplay(xinfo.display);

    return EXIT_SUCCESS;
}
