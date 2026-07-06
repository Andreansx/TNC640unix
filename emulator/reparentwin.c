/* reparentwin.c — force the OEM-window-adoption gate: reparent Guppy's HwViewer
 * top-level onto winmgr's OemScreen so GetDecorationSize (its QueryTree/GetGeometry
 * poll) succeeds and Guppy proceeds to jh.softkey.Register. Faithful forced trigger:
 * does exactly what a WM would do (reparent client -> screen). Persists in a loop
 * because Guppy may re-create/the poll re-checks.
 *   usage: reparentwin <child 0x..> <parent 0x..> <seconds>  (0=find HwViewer/OEM by name)
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Window by_name(Display*d, Window root, const char*want){
    Window r,p,*kids=0; unsigned n=0; Window found=0;
    if(!XQueryTree(d,root,&r,&p,&kids,&n)) return 0;
    for(unsigned i=0;i<n && !found;i++){ char*nm=0; if(XFetchName(d,kids[i],&nm)&&nm){ if(strstr(nm,want)) found=kids[i]; XFree(nm);} }
    if(kids)XFree(kids); return found;
}
int main(int argc,char**argv){
    if(argc<4){fprintf(stderr,"usage: %s <child|0> <parent|0> <secs>\n",argv[0]);return 2;}
    Display*d=XOpenDisplay(NULL); if(!d){fprintf(stderr,"no display\n");return 1;}
    Window root=DefaultRootWindow(d);
    Window child=strtoul(argv[1],0,0), parent=strtoul(argv[2],0,0);
    int secs=atoi(argv[3]); int done=0;
    for(int t=0;t<secs*2;t++){
        Window c=child?child:by_name(d,root,"HwViewer");
        Window pw=parent?parent:by_name(d,root,"OEM");
        if(c&&pw){
            XWindowAttributes ca; if(XGetWindowAttributes(d,c,&ca)){
                if(ca.root && /*only if still top-level under root*/ 1){
                    XReparentWindow(d,c,pw,0,0);
                    XMapWindow(d,pw); XMapWindow(d,c); XRaiseWindow(d,c);
                    XSync(d,False);
                    if(!done){ printf("reparented child=0x%lx into parent=0x%lx (%dx%d)\n",c,pw,ca.width,ca.height); fflush(stdout); done=1; }
                }
            }
        }
        usleep(500000);
    }
    printf("reparentwin done (child%s reparented)\n", done?"":" NOT");
    XCloseDisplay(d); return 0;
}
