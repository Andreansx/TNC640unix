#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK11GestureArea15preventStealingEv(void*);
extern int _ZNK11GestureArea20isMouseDoubleClickedEv(void*);
extern int _ZNK11GestureArea18isMultiTouchActiveEv(void*);
extern int _ZNK11GestureArea14isMouseEnabledEv(void*);
extern int _ZNK11GestureArea9timestampEv(void*);
extern int _ZN11GestureArea18ignoreCurrentEventEv(void*);
extern int _ZN11GestureArea17forceReleaseEventEv(void*);
extern int _ZN11GestureArea23isSynthesizedMouseEventEv(void*);
extern int _ZNK11GestureArea12mouseHoveredEv(void*);
extern int _ZNK11GestureArea17mouseHoverEnabledEv(void*);
extern int _ZNK11GestureArea30propagateSynthesizedMouseEventEv(void*);
int main(void){char buf[64];
{unsigned char o[1024],d[1024];fill(o,1024,1);fill(d,1024,3);*(void**)(o+16)=d;int r=_ZNK11GestureArea15preventStealingEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,8);fill(d,1024,16);*(void**)(o+16)=d;int r=_ZNK11GestureArea20isMouseDoubleClickedEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,15);fill(d,1024,29);*(void**)(o+16)=d;int r=_ZNK11GestureArea18isMultiTouchActiveEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,22);fill(d,1024,42);*(void**)(o+16)=d;int r=_ZNK11GestureArea14isMouseEnabledEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,29);fill(d,1024,55);*(void**)(o+16)=d;int r=_ZNK11GestureArea9timestampEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,36);fill(d,1024,68);*(void**)(o+16)=d;int r=_ZN11GestureArea18ignoreCurrentEventEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,43);fill(d,1024,81);*(void**)(o+16)=d;int r=_ZN11GestureArea17forceReleaseEventEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,50);fill(d,1024,94);*(void**)(o+16)=d;int r=_ZN11GestureArea23isSynthesizedMouseEventEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,57);fill(d,1024,107);*(void**)(o+16)=d;int r=_ZNK11GestureArea12mouseHoveredEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,64);fill(d,1024,120);*(void**)(o+16)=d;int r=_ZNK11GestureArea17mouseHoverEnabledEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,71);fill(d,1024,133);*(void**)(o+16)=d;int r=_ZNK11GestureArea30propagateSynthesizedMouseEventEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
return 0;}
