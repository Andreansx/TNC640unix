#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK22FlServerRecordInStream11MoreRecordsEv(void*);
extern int _ZNK22FlServerRecordIoStream11MoreRecordsEv(void*);
extern int _ZNK15FN16MaskPrinter10CloseFoundEv(void*);
extern int _ZNK15FN16MaskPrinter11AppendFoundEv(void*);
extern int _ZN15NcProgramStream10FileServerEv(void*);
extern int _ZNK12TncAbProgram10TextStreamEv(void*);
extern int _ZNK12TncBaProgram12BinaryStreamEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK22FlServerRecordInStream11MoreRecordsEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK22FlServerRecordIoStream11MoreRecordsEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK15FN16MaskPrinter10CloseFoundEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK15FN16MaskPrinter11AppendFoundEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZN15NcProgramStream10FileServerEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK12TncAbProgram10TextStreamEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK12TncBaProgram12BinaryStreamEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
return 0;}
