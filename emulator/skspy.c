/* skspy.c — LD_PRELOAD interposer to trace the SkMgrCtrl connection lifecycle.
 * Hooks (inter-lib, GOT-routed → interposable since the libs aren't -Bsymbolic):
 *   FMailslotQueue::Open(this, astring& name, bool send)      libbackend
 *   KernelInterfaceObjectManager::Register(this, KernelInterface*)  libNcCtrlModule
 *   KernelInterfaceObjectManager::Find(this, astring class_id, int) libNcCtrlModule
 * Logs to stderr (prefixed [skspy]) so we see whether the SkMgrCtrlInterface is
 * registered, found, and whether the "Q_SkMgr" transport opens.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o skspy.so emulator/skspy.c -ldl
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

/* Best-effort read of a HEIDENHAIN BasicString<char>* — try common layouts:
 * the data pointer is at one of offsets 0/4/8; pick the one that points to printable text. */
static const char* astr(const void* p){
  static __thread char buf[128];
  if(!p) return "(null)";
  for(int off=0; off<=8; off+=4){
    const char* cp = *(const char* const*)((const char*)p + off);
    if(cp){
      /* sanity: printable, NUL-terminated within 80 chars */
      int ok=1, i=0;
      for(; i<80; i++){ char c=cp[i]; if(c==0) break; if(c<0x20||(unsigned char)c>0x7e){ok=0;break;} }
      if(ok && i>0 && i<80){ snprintf(buf,sizeof buf,"%s",cp); return buf; }
    }
  }
  return "(?)";
}

typedef unsigned char (*open_t)(void*, const void*, unsigned char);
typedef void  (*reg_t)(void*, void*);
typedef int   (*sendmsg_t)(void*, int, int);

static open_t real_open;
static reg_t  real_reg;
static sendmsg_t real_sendmsg;

/* SkMgrCtrlInterfaceImpl::SendMessage(this, SKMGRCTRL_REQUESTTYPE, GMessage&) */
int _ZN22SkMgrCtrlInterfaceImpl11SendMessageEN16SkMgrCtrlDefines21SKMGRCTRL_REQUESTTYPEER8GMessage(void* self, int reqtype, int gmsg){
  if(!real_sendmsg) real_sendmsg=(sendmsg_t)dlsym(RTLD_NEXT,"_ZN22SkMgrCtrlInterfaceImpl11SendMessageEN16SkMgrCtrlDefines21SKMGRCTRL_REQUESTTYPEER8GMessage");
  unsigned char connflag = *((unsigned char*)self + 48);
  int r = real_sendmsg(self, reqtype, gmsg);
  /* return codes: 3=not connected, 5=bad state, 4=send failed, 0=OK */
  fprintf(stderr,"[skspy] SkMgrCtrl::SendMessage(reqtype=%d, conn48=%d) -> %d\n", reqtype, connflag, r);
  fflush(stderr);
  return r;
}

/* FMailslotQueue::Open(this, astring& name, bool send) */
unsigned char _ZN14FMailslotQueue4OpenERK7astringb(void* self, const void* name, unsigned char send){
  if(!real_open) real_open=(open_t)dlsym(RTLD_NEXT,"_ZN14FMailslotQueue4OpenERK7astringb");
  unsigned char r = real_open(self, name, send);
  fprintf(stderr,"[skspy] FMailslotQueue::Open(name=\"%s\", send=%d) -> %d\n", astr(name), send, r);
  fflush(stderr);
  return r;
}

/* KernelInterfaceObjectManager::Register(this, KernelInterface* iface)
 * iface CLASS_ID astring is at iface+4 (per KernelInterfaceHdl::Connect: operator==(iface+4, CLASS_ID)) */
void _ZN28KernelInterfaceObjectManager8RegisterEP15KernelInterface(void* self, void* iface){
  if(!real_reg) real_reg=(reg_t)dlsym(RTLD_NEXT,"_ZN28KernelInterfaceObjectManager8RegisterEP15KernelInterface");
  fprintf(stderr,"[skspy] KIOM::Register(iface=%p class_id=\"%s\")\n", iface, astr((const char*)iface+4));
  fflush(stderr);
  real_reg(self, iface);
}
