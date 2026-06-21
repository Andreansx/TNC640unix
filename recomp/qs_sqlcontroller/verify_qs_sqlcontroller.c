#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZN3qic20AbstractTableRequest5stateEv(void*);
extern int _ZNK3qic16ConnectionHandle16connectionHandleEv(void*);
extern int _ZN3qic8DataBase11connectionsEv(void*);
extern int _ZNK3qic9RecordSet5stateEv(void*);
extern int _ZNK3qic9RecordSet16hasCachedRecordsEv(void*);
extern int _ZNK3qic9RecordSet10columnDataEv(void*);
extern int _ZThn8_NK3qic9RecordSet10columnDataEv(void*);
extern int _ZNK3qic9RecordSet10autoCommitEv(void*);
extern int _ZNK3qic19RecordSetHeaderData16GetPrimaryColumnEv(void*);
extern int _ZNK3qic18RecordSetOperation9errorDataEv(void*);
extern int _ZN3qic22TableIsWritableRequest13writableStateEv(void*);
extern int _ZNK3qic22RecordSetSelectContext4sizeEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZN3qic20AbstractTableRequest5stateEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK3qic16ConnectionHandle16connectionHandleEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZN3qic8DataBase11connectionsEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK3qic9RecordSet5stateEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK3qic9RecordSet16hasCachedRecordsEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK3qic9RecordSet10columnDataEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZThn8_NK3qic9RecordSet10columnDataEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK3qic9RecordSet10autoCommitEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZNK3qic19RecordSetHeaderData16GetPrimaryColumnEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZNK3qic18RecordSetOperation9errorDataEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,71);int r=_ZN3qic22TableIsWritableRequest13writableStateEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZNK3qic22RecordSetSelectContext4sizeEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
return 0;}
