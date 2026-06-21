#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK18EmbedWindowTexture8windowIdEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK18EmbedWindowTexture8windowIdEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
return 0;}
