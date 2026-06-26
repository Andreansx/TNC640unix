/* wmstub.c — LD_PRELOAD: trace (and optionally force-succeed) the HeROS window-manager
 * registration path that gates softkey binding. Default = LOG pass-through; WMFORCE=1 =
 * force WM RPCs to succeed (so the GTK window becomes a bind-capable WndFullScreen without winmgr).
 *
 * Chain: GuppyRuntimeGtk::Register -> GuppyRegisterWindow -> gtk_window_set_usage ->
 *        {gtk_wm_window_set_usage | gdk_wm_set_usage} -> WmRegisterWindowEx -> WmSendRequestReply
 *        (-> WmWrite Q_WMGR / WmRead reply). No winmgr => the reply never comes.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o wmstub.so emulator/wmstub.c -ldl
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static int force(void){ static int f=-1; if(f<0){ const char*e=getenv("WMFORCE"); f=(e&&*e=='1'); } return f; }

typedef int (*i_t)();
#define REAL(name,var) static i_t var; if(!var) var=(i_t)dlsym(RTLD_NEXT,name)

/* int gtk_window_set_usage(GtkWidget*, uint screen, char* usage, int) — nonzero=success */
int gtk_window_set_usage(void* w, unsigned scr, char* usage, int a4){
  REAL("gtk_window_set_usage", r);
  int rc = r ? ((int(*)(void*,unsigned,char*,int))r)(w,scr,usage,a4) : 1;
  fprintf(stderr,"[wmstub] gtk_window_set_usage(usage=\"%s\" scr=%u) -> %d%s\n",
          usage?usage:"(null)", scr, rc, (force()&&rc==0)?" [FORCED->1]":"");
  fflush(stderr);
  if(force() && rc==0) return 1;
  return rc;
}

/* int gtk_wm_window_set_usage(GtkWmWindow*, char* usage, int) — nonzero=success */
int gtk_wm_window_set_usage(void* w, char* usage, int a3){
  REAL("gtk_wm_window_set_usage", r);
  int rc = r ? ((int(*)(void*,char*,int))r)(w,usage,a3) : 1;
  fprintf(stderr,"[wmstub] gtk_wm_window_set_usage(usage=\"%s\") -> %d%s\n",
          usage?usage:"(null)", rc, (force()&&rc==0)?" [FORCED->1]":"");
  fflush(stderr);
  if(force() && rc==0) return 1;
  return rc;
}

/* int WmRegisterWindowEx(...) — 0=success */
int WmRegisterWindowEx(void* ctx, void* dpy, int xid, int screen, char* usage, int a6, int a7){
  fprintf(stderr,"[wmstub] WmRegisterWindowEx(xid=0x%x scr=%d usage=\"%s\")%s\n",
          xid, screen, usage?usage:"(null)", force()?" -> forced 0":"");
  fflush(stderr);
  if(force()) return 0;
  REAL("WmRegisterWindowEx", r);
  return r ? ((int(*)(void*,void*,int,int,char*,int,int))r)(ctx,dpy,xid,screen,usage,a6,a7) : 0;
}

/* int WmSendRequestReply(int* ctx, dword* req, int reqsz, int* reply, int replysz, int* a6) — 0=success */
int WmSendRequestReply(int* ctx, unsigned* req, int reqsz, int* reply, int replysz, int* a6){
  unsigned cmd = req? req[0] : 0;
  fprintf(stderr,"[wmstub] WmSendRequestReply(cmd=0x%x reqsz=%d ctx=%p)%s\n",
          cmd, reqsz, (void*)ctx, force()?" -> forced 0 (zeroed reply)":"");
  fflush(stderr);
  if(force()){ if(reply && replysz>0) memset(reply,0,replysz); return 0; }
  REAL("WmSendRequestReply", r);
  return r ? ((int(*)(int*,unsigned*,int,int*,int,int*))r)(ctx,req,reqsz,reply,replysz,a6) : 0;
}
