/* wmembed — minimal X window-placement helper for the FEX-native 3-proc HwViewer run.
 *
 * NOTE (2026-06-26 RE correction): the softkey BAR is NOT drawn in Guppy's GTK window — it is drawn by
 * SKMGR (which loads the .bmx via SkMgrSYSResource::ParseSoftkey->LoadImage and has the PLib softkey
 * symbols PFrame::GetSoftkeyRootId/SkMgrShowSoftkey) into the winmgr-managed *softkey-area window* from
 * the screen layout (tnc640layout1280.xml VSoftKeyArea). winmgr creates that window in
 * WmModule::Initialize->CreateMainWindow/ReadLayout->XCreateWindow, which never fires standalone (it is
 * gated on the AppStartMaster FModule start directive). So merely resizing Guppy's GTK window does NOT
 * render the bar — the bar needs the winmgr screen-window. This helper is therefore a STARTING POINT for
 * a faithful "winmgr-window stand-in" (lever (a) in scratchpad/skmgr_softkey_findings.md): it must be
 * extended to CREATE the real screen-layout X windows (incl the softkey-area rect) and feed their xids
 * back to skmgr/PLib via the WM replies, not just XMoveResizeWindow an existing GTK window. As written it
 * only polls for a window by WM_NAME and XMoveResizeWindow's it fullscreen (a diagnostic for the separate
 * HwViewer-GTK-window-not-fullscreen issue). Runs in the lima VM (aarch64) against DISPLAY=:0.
 *
 * No X11 headers are installed in the VM, only libX11.so.6, so the few Xlib entry points are declared
 * by hand and linked with -l:libX11.so.6.  Build:
 *   gcc -O2 -o wmembed emulator/wmembed.c -l:libX11.so.6
 * Run (in the run script, after Xvfb is up):
 *   DISPLAY=:0 ./wmembed 1280 936 HwViewer &
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void Display;
extern Display *XOpenDisplay(const char *);
extern unsigned long XDefaultRootWindow(Display *);
extern int XQueryTree(Display *, unsigned long, unsigned long *, unsigned long *,
                      unsigned long **, unsigned *);
extern int XFetchName(Display *, unsigned long, char **);
extern int XMoveResizeWindow(Display *, unsigned long, int, int, unsigned, unsigned);
extern int XMapWindow(Display *, unsigned long);
extern int XRaiseWindow(Display *, unsigned long);
extern int XFlush(Display *);
extern int XSync(Display *, int);
extern int XFree(void *);
/* XGetGeometry(dpy, drawable, &root, &x, &y, &w, &h, &border, &depth) */
extern int XGetGeometry(Display *, unsigned long, unsigned long *, int *, int *,
                        unsigned *, unsigned *, unsigned *, unsigned *);

int main(int argc, char **argv) {
    int W = argc > 1 ? atoi(argv[1]) : 1280;
    int H = argc > 2 ? atoi(argv[2]) : 936;
    const char *target = argc > 3 ? argv[3] : "HwViewer";
    const char *dname = getenv("DISPLAY");
    Display *d = XOpenDisplay(dname);
    if (!d) { fprintf(stderr, "wmembed: cannot open DISPLAY=%s\n", dname ? dname : "(null)"); return 1; }
    unsigned long root = XDefaultRootWindow(d);
    fprintf(stderr, "wmembed: DISPLAY=%s target=\"%s\" -> %dx%d @ (0,0)\n", dname, target, W, H);
    int loops = 0, hits = 0;
    for (loops = 0; loops < 20000; loops++) {
        unsigned long r, parent, *kids = 0; unsigned n = 0;
        if (XQueryTree(d, root, &r, &parent, &kids, &n)) {
            for (unsigned i = 0; i < n; i++) {
                char *nm = 0;
                XFetchName(d, kids[i], &nm);
                if (nm && !strcmp(nm, target)) {
                    XMoveResizeWindow(d, kids[i], 0, 0, (unsigned)W, (unsigned)H);
                    XMapWindow(d, kids[i]);
                    XRaiseWindow(d, kids[i]);
                    if (!hits) fprintf(stderr, "wmembed: embedding \"%s\" win 0x%lx -> %dx%d\n", nm, kids[i], W, H);
                    hits++;
                }
                /* one-shot topology dump on the first pass */
                if (loops == 0) {
                    unsigned long gr; int gx, gy; unsigned gw, gh, gb, gd;
                    if (XGetGeometry(d, kids[i], &gr, &gx, &gy, &gw, &gh, &gb, &gd))
                        fprintf(stderr, "wmembed: win 0x%lx \"%s\" %ux%u @ (%d,%d)\n",
                                kids[i], nm ? nm : "(noname)", gw, gh, gx, gy);
                }
                if (nm) XFree(nm);
            }
            if (kids) XFree(kids);
        }
        XFlush(d); XSync(d, 0);
        usleep(300000);
    }
    fprintf(stderr, "wmembed: done (%d embeds)\n", hits);
    return 0;
}
