#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK3Plc6Server4Jobs4Impl7Inspect14GetReplyFormatEv(void*);
extern int _ZNK3Plc6Server4Jobs4Impl19InspectSubscription9GetOptionEv(void*);
extern int _ZNK3Plc6Server4Jobs4Impl7Request12GetJobHandleEv(void*);
extern int _ZNK3Plc6Server4Jobs4Impl7Request22GetPendingAbortRequestEv(void*);
extern int _ZNK10DataWriter13GetAccessListEv(void*);
extern int _ZNK10DataWriter23SynchronizeWithPlcCycleEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK3Plc6Server4Jobs4Impl7Inspect14GetReplyFormatEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK3Plc6Server4Jobs4Impl19InspectSubscription9GetOptionEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,15);fill(d,1024,29);*(void**)(o+4)=d;int r=_ZNK3Plc6Server4Jobs4Impl7Request12GetJobHandleEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024],d[1024];fill(o,1024,22);fill(d,1024,42);*(void**)(o+4)=d;int r=_ZNK3Plc6Server4Jobs4Impl7Request22GetPendingAbortRequestEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK10DataWriter13GetAccessListEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK10DataWriter23SynchronizeWithPlcCycleEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
return 0;}
