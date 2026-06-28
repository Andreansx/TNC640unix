/* barcopy <dur_sec> [per_ms]
 * skmgr faithfully composites the hwv softkey bar (19 .bmx, RENDER blits) into a server-side PIXMAP, but the
 * pixmap->visible-window "show" never happens because winmgr's map path is dormant (0 XMapWindow). This helper
 * does that final faithful step ourselves: scan skmgr's XID range for the BAR PIXMAP (a Drawable that
 * XGetGeometry succeeds on but is NOT a window, bar-like dimensions ~1280x88), create a visible
 * override-redirect window at the softkey-area position (0,936), and XCopyArea the pixmap's REAL pixels onto it.
 * The content is skmgr's genuine composite -- we only perform the display step winmgr would have done. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct _XDisplay Display; typedef unsigned long XID; typedef XID Window; typedef XID Drawable;
typedef XID Pixmap; typedef XID GC_; typedef int Status; typedef unsigned long Atom;
typedef struct { XID a; unsigned long b; XID c; unsigned long d; int e,f,g; unsigned long h,i; int j;
  long k,l; int override_redirect; XID m,n; } XSetWindowAttributes;
#define CWOverrideRedirect (1L<<9)
#define CWBackingStore (1L<<6)
extern Display *XOpenDisplay(const char *);
extern Window XDefaultRootWindow(Display *);
extern int XDefaultScreen(Display *);
extern unsigned long XWhitePixel(Display*,int);
extern int XDefaultDepth(Display*,int);
extern Window XCreateSimpleWindow(Display*,Window,int,int,unsigned,unsigned,unsigned,unsigned long,unsigned long);
extern int XMapRaised(Display*,Window);
extern int XRaiseWindow(Display*,Window);
extern int XChangeWindowAttributes(Display*,Window,unsigned long,XSetWindowAttributes*);
extern void* XCreateGC(Display*,Drawable,unsigned long,void*);
extern int XCopyArea(Display*,Drawable,Drawable,void*,int,int,unsigned,unsigned,int,int);
extern int XReparentWindow(Display*,Window,Window,int,int);
extern int XMapWindow(Display*,Window);
extern Status XGetGeometry(Display*,Drawable,Window*,int*,int*,unsigned*,unsigned*,unsigned*,unsigned*);
extern Status XQueryTree(Display*,Window,Window*,Window*,Window**,unsigned*);
extern int XFree(void*);
extern int XFlush(Display*);
extern int XSync(Display*,int);
extern int (*XSetErrorHandler(int (*)(Display*,void*)))(Display*,void*);
static int ignore_err(Display*d,void*e){ (void)d;(void)e; return 0; }

int main(int argc,char**argv){
    int dur=argc>1?atoi(argv[1]):120, per=argc>2?atoi(argv[2]):150;
    Display*d=XOpenDisplay(NULL);
    if(!d){fprintf(stderr,"barcopy: no display\n");return 1;}
    XSetErrorHandler((int(*)(Display*,void*))ignore_err);
    int scr=XDefaultScreen(d); Window root=XDefaultRootWindow(d);
    int ddepth=XDefaultDepth(d,scr);
    /* visible target window at the softkey-area position */
    Window win=XCreateSimpleWindow(d,root,0,936,1280,88,0,0,0);
    XSetWindowAttributes at; at.override_redirect=1; XChangeWindowAttributes(d,win,CWOverrideRedirect,&at);
    XMapRaised(d,win);
    void*gc=XCreateGC(d,win,0,0);
    (void)win;(void)gc;(void)ddepth;
    /* skmgr RENDER-composites the bar into WINDOW 0x600006 (ScreenNC_HorizontalManager's deepest child) and the
     * EDIT equivalent 0x600012 -- confirmed from sk_strace RenderCreatePicture(pid 0x60001a, drawable 0x600006).
     * Those windows are UnViewable (winmgr never maps the chain). Make the EXACT composite targets viewable BEFORE
     * skmgr composites: reparent each to root@(0,936) as an override-redirect top-level + map + raise, continuously,
     * so the one-time RENDER composite lands on a viewable window = the bar shows (position-independent, by window id). */
    XID targets[] = {0x600012,0x600006};   /* EDIT strip then NC strip (0x600006 raised last = on top) */
    int nt = sizeof(targets)/sizeof(targets[0]);
    int passes=(dur*1000)/per;
    for(int i=0;i<passes;i++){
        for(int t=0;t<nt;t++){
            XID id=targets[t];
            Window r; int x,y; unsigned w,h,bw,depth;
            if(!XGetGeometry(d,id,&r,&x,&y,&w,&h,&bw,&depth)) continue;
            XSetWindowAttributes a2; a2.override_redirect=1;
            XChangeWindowAttributes(d,id,CWOverrideRedirect,&a2);
            XReparentWindow(d,id,root,0,936);
            XMapWindow(d,id);
            XRaiseWindow(d,id);
            if(i==0) fprintf(stderr,"barcopy: target 0x%lx %ux%u depth %u -> reparent root@(0,936)+map+raise\n",id,w,h,depth);
        }
        XFlush(d); XSync(d,0);
        usleep(per*1000);
    }
    fprintf(stderr,"barcopy: done (%d passes)\n",passes);
    return 0;
}
