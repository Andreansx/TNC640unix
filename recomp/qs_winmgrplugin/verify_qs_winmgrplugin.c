#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK16EmbeddableWindow14embeddedActiveEv(void*);
extern int _ZNK16EmbeddableWindow8inLayoutEv(void*);
extern int _ZNK16EmbeddableWindow13focusOnLayoutEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK16EmbeddableWindow14embeddedActiveEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK16EmbeddableWindow8inLayoutEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK16EmbeddableWindow13focusOnLayoutEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
return 0;}
