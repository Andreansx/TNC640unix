/* wmembed — winmgr X-level window-placement stand-in for the FEX-native 3-proc HwViewer run.
 *
 * In the real control, winmgr's HandleMessage@0x29f00 case 0x300c (window embed) reparents + resizes the
 * client's WndFullScreen into the OemScreen ClientArea rect (fullscreen, frameless) at the X level. In the
 * 3-proc setup there is no winmgr, so the WndFullScreen keeps its GTK glade natural size (~330x165) and the
 * softkey bar (bottom strip, HwViewer.py logScreenSize=[1280,936]) has nowhere to draw. This helper performs
 * winmgr's X-level placement: it polls the X server, finds the proprietary client window(s), and
 * XMoveResizeWindow's the main one to the requested fullscreen rect, looping so a later GTK size
 * re-allocation cannot shrink it back. It runs in the lima VM (aarch64) against DISPLAY=:0 — NOT in the FEX
 * guest. No X11 headers are installed in the VM, only libX11.so.6, so the Xlib entry points are declared by
 * hand. Build:  gcc -O2 -o wmembed emulator/wmembed.c -l:libX11.so.6
 * Run:   DISPLAY=:0 ./wmembed [W H]            (default 1280 1024 = full screen)
 *
 * Matching: the WndFullScreen is an override-redirect (POPUP, usage=PyLargeMachine) top-level. We resize
 * EVERY mapped child of root whose current size is in a "client window" band (>=120x90, not the root, not a
 * 1x1 openbox helper) to WxH@(0,0). DUMPONLY=1 only prints the topology (no resize) for diagnosis.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void Display;
extern Display *XOpenDisplay(const char *);
extern unsigned long XDefaultRootWindow(Display *);
extern int XQueryTree(Display *, unsigned long, unsigned long *, unsigned long *, unsigned long **, unsigned *);
extern int XFetchName(Display *, unsigned long, char **);
extern int XMoveResizeWindow(Display *, unsigned long, int, int, unsigned, unsigned);
extern int XResizeWindow(Display *, unsigned long, unsigned, unsigned);
extern int XMapWindow(Display *, unsigned long);
extern int XRaiseWindow(Display *, unsigned long);
extern int XFlush(Display *);
extern int XSync(Display *, int);
extern int XFree(void *);
extern int XGetGeometry(Display *, unsigned long, unsigned long *, int *, int *, unsigned *, unsigned *, unsigned *, unsigned *);
/* XGetWindowAttributes needs a struct; we only need map_state + override_redirect, which sit at known
 * offsets in the XWindowAttributes struct (x,y,width,height,border,depth,visual,root,class,bit_gravity,
 * win_gravity,backing_store,...). Simpler: use XGetGeometry for size + treat all mapped sized windows. */

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1280;
    int H = argc > 2 ? atoi(argv[2]) : 1024;
    int dumponly = getenv("DUMPONLY") && getenv("DUMPONLY")[0]=='1';
    const char *dname = getenv("DISPLAY");
    Display *d = XOpenDisplay(dname);
    if (!d) { fprintf(stderr, "wmembed: cannot open DISPLAY=%s\n", dname?dname:"(null)"); return 1; }
    unsigned long root = XDefaultRootWindow(d);
    fprintf(stderr, "wmembed: DISPLAY=%s -> resize client windows to %dx%d@(0,0) (dumponly=%d)\n", dname, W, H, dumponly);
    int loops, hits = 0, dumped = 0;
    for (loops = 0; loops < 9000; loops++) {
        unsigned long r, parent, *kids = 0; unsigned n = 0;
        if (XQueryTree(d, root, &r, &parent, &kids, &n)) {
            for (unsigned i = 0; i < n; i++) {
                char *nm = 0; XFetchName(d, kids[i], &nm);
                unsigned long gr; int gx, gy; unsigned gw, gh, gb, gd;
                int ok = XGetGeometry(d, kids[i], &gr, &gx, &gy, &gw, &gh, &gb, &gd);
                if (ok && (loops==0 || (dumped<3 && hits==0))) {
                    fprintf(stderr, "wmembed: win 0x%lx \"%s\" %ux%u @ (%d,%d)\n", kids[i], nm?nm:"(noname)", gw, gh, gx, gy);
                }
                /* a client content window: sized (not 1x1 helper), not already fullscreen */
                if (ok && !dumponly && gw>=120 && gh>=90 && (gw!=(unsigned)W || gh!=(unsigned)H)) {
                    XMoveResizeWindow(d, kids[i], 0, 0, (unsigned)W, (unsigned)H);
                    XMapWindow(d, kids[i]); XRaiseWindow(d, kids[i]);
                    if (hits<8) fprintf(stderr, "wmembed: RESIZE 0x%lx \"%s\" %ux%u -> %dx%d\n", kids[i], nm?nm:"(noname)", gw, gh, W, H);
                    hits++;
                }
                if (nm) XFree(nm);
            }
            dumped++;
            if (kids) XFree(kids);
        }
        XFlush(d); XSync(d, 0);
        usleep(300000);
    }
    fprintf(stderr, "wmembed: done (%d resizes)\n", hits);
    return 0;
}
