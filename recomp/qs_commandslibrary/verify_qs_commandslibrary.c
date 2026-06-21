#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK33AbstractConfigurableFutureCommand10canExecuteEv(void*);
extern int _ZNK27AbstractConfigurableCommand10canExecuteEv(void*);
extern int _ZNK17RenderableCommand7commandEv(void*);
extern int _ZNK17RenderableCommand9checkableEv(void*);
extern int _ZNK17RenderableCommand7checkedEv(void*);
extern int _ZNK17RenderableCommand15doubleSeparatorEv(void*);
extern int _ZNK17RenderableCommand7recolorEv(void*);
extern int _ZNK17RenderableCommand7visibleEv(void*);
extern int _ZNK21AbstractFutureCommand9isRunningEv(void*);
extern int _ZNK21AbstractFutureCommand11futureCountEv(void*);
extern int _ZNK22CompositeFutureCommand13executionModeEv(void*);
extern int _ZNK14ExecutionError4codeEv(void*);
extern int _ZNK23AbstractStandardCommand10canExecuteEv(void*);
extern int _ZNK27AbstractSyncStandardCommand10canExecuteEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK33AbstractConfigurableFutureCommand10canExecuteEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK27AbstractConfigurableCommand10canExecuteEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK17RenderableCommand7commandEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK17RenderableCommand9checkableEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK17RenderableCommand7checkedEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK17RenderableCommand15doubleSeparatorEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK17RenderableCommand7recolorEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK17RenderableCommand7visibleEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZNK21AbstractFutureCommand9isRunningEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,64);fill(d,1024,120);*(void**)(o+12)=d;int r=_ZNK21AbstractFutureCommand11futureCountEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,71);fill(d,1024,133);*(void**)(o+20)=d;int r=_ZNK22CompositeFutureCommand13executionModeEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZNK14ExecutionError4codeEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,85);int r=_ZNK23AbstractStandardCommand10canExecuteEv(o);int n=sprintf(buf,"%d %d\n",12,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,92);int r=_ZNK27AbstractSyncStandardCommand10canExecuteEv(o);int n=sprintf(buf,"%d %d\n",13,r);write(1,buf,n);}
return 0;}
