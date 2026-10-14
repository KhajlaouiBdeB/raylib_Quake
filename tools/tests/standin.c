/* QA fixture only: an X11 window and scripted logs, never a Quake engine. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int no_ready = 0, no_window = 0, demo = 0, timedemo = 0;
    int no_metric = 0, no_demo = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--fixture-exit")) return 7;
        if (!strcmp(argv[i], "--fixture-error")) {
            fprintf(stderr, "Host_Error: simulated failure\n");
            return 9;
        }
        no_ready |= !strcmp(argv[i], "--fixture-no-ready");
        no_window |= !strcmp(argv[i], "--fixture-no-window");
        no_metric |= !strcmp(argv[i], "--fixture-no-metric");
        no_demo |= !strcmp(argv[i], "--fixture-no-demo");
        demo |= !strcmp(argv[i], "+startdemos");
        timedemo |= !strcmp(argv[i], "+timedemo");
    }
    Display *display = XOpenDisplay(NULL);
    if (!display) return 10;
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                        0, 0, 640, 400, 0, 0, 0x183040);
    unsigned long pid = (unsigned long)getpid();
    XChangeProperty(display, window, XInternAtom(display, "_NET_WM_PID", False),
                    XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&pid, 1);
    XStoreName(display, window, "QA STAND-IN (not Quake)");
    if (!no_window) XMapWindow(display, window);
    GC gc = XCreateGC(display, window, 0, NULL);
    XSetForeground(display, gc, 0xffffff);
    XFlush(display);
    if (!no_ready) puts("========Quake Initialized========= [fixture]");
    fprintf(stderr, "QA fixture stderr collected; no engine execution\n");
    fflush(stdout);
    const char *demos[] = {"demo1", "demo2", "demo3", "demo1"};
    for (int tick = 0; ; ++tick) {
        if (demo && !no_demo && tick >= 10 && tick <= 40 && tick % 10 == 0)
            printf("Playing demo from %s.dem.\n", demos[tick / 10 - 1]);
        if (timedemo && !no_metric && tick == 10) puts("100 frames   1.0 seconds 100.0 fps");
        fflush(stdout);
        if (!no_window) {
            const char *label = "QA STAND-IN: control flow only; NOT Quake gameplay";
            XDrawString(display, window, gc, 40, 60, label, (int)strlen(label));
            XFlush(display);
        }
        struct timespec delay = {0, 100000000};
        nanosleep(&delay, NULL);
    }
}
