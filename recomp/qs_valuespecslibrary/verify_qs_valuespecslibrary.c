#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK17UnsignedValueSpec4baseEv(void*);
extern int _ZNK9ValueSpec20valueSpecRangeFillerEv(void*);
extern int _ZNK9ValueSpec9alignmentEv(void*);
extern int _ZNK9ValueSpec10editorKindEv(void*);
extern int _ZNK9ValueSpec18numberOfCharactersEv(void*);
extern int _ZNK9ValueSpec19isEmptyValueAllowedEv(void*);
extern int _ZNK9ValueSpec21autoCorrectLetterCaseEv(void*);
extern int _ZNK9ValueSpec27warnImmediatelyOnEmptyValueEv(void*);
extern int _ZNK9ValueSpec13lastErrorTypeEv(void*);
extern int _ZNK15DoubleValueSpec13applyDecimalsEv(void*);
extern int _ZNK15DoubleValueSpec10nrDecimalsEv(void*);
extern int _ZNK17UnsignedValueSpec15maxStringLengthEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK17UnsignedValueSpec4baseEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK9ValueSpec20valueSpecRangeFillerEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK9ValueSpec9alignmentEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK9ValueSpec10editorKindEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK9ValueSpec18numberOfCharactersEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK9ValueSpec19isEmptyValueAllowedEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK9ValueSpec21autoCorrectLetterCaseEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK9ValueSpec27warnImmediatelyOnEmptyValueEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZNK9ValueSpec13lastErrorTypeEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZNK15DoubleValueSpec13applyDecimalsEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,71);int r=_ZNK15DoubleValueSpec10nrDecimalsEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZNK17UnsignedValueSpec15maxStringLengthEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
return 0;}
