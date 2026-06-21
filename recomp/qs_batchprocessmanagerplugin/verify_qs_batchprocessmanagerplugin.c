#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK3bpm13Configuration6stopAtEv(void*);
extern int _ZNK3bpm13Configuration13optionInitBPMEv(void*);
extern int _ZNK3bpm13Configuration18optionAvailableDCMEv(void*);
extern int _ZNK3bpm13Configuration18isToolMgtActivatedEv(void*);
extern int _ZNK7JobData26getControlInOperationStateEv(void*);
extern int _ZNK7JobData27getPositionUpdateInProgressEv(void*);
extern int _ZNK7JobData21getLastNumberExecutedEv(void*);
extern int _ZNK7JobData18getStartRecordBaseEv(void*);
extern int _ZThn8_NK7JobData18getStartRecordBaseEv(void*);
extern int _ZNK7JobData31getStartRecordDerivedForNcStartEv(void*);
extern int _ZNK7JobData31getStartRecordDerivedForDisplayEv(void*);
extern int _ZN10JobHandler13loadingStatusEv(void*);
extern int _ZNK12JobListModel9editIndexEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK3bpm13Configuration6stopAtEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK3bpm13Configuration13optionInitBPMEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK3bpm13Configuration18optionAvailableDCMEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK3bpm13Configuration18isToolMgtActivatedEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK7JobData26getControlInOperationStateEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK7JobData27getPositionUpdateInProgressEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK7JobData21getLastNumberExecutedEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK7JobData18getStartRecordBaseEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZThn8_NK7JobData18getStartRecordBaseEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZNK7JobData31getStartRecordDerivedForNcStartEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,71);int r=_ZNK7JobData31getStartRecordDerivedForDisplayEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZN10JobHandler13loadingStatusEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,85);int r=_ZNK12JobListModel9editIndexEv(o);int n=sprintf(buf,"%d %d\n",12,r);write(1,buf,n);}
return 0;}
