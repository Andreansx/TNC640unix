#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK16GlobalConfigData18comoHideStatusTabsEv(void*);
extern int _ZNK16GlobalConfigData12warningAtDELEv(void*);
extern int _ZNK16GlobalConfigData11noParaxModeEv(void*);
extern int _ZNK16GlobalConfigData10deleteBackEv(void*);
extern int _ZNK16GlobalConfigData11useProgAxesEv(void*);
extern int _ZNK16GlobalConfigData12createBackupEv(void*);
extern int _ZNK16GlobalConfigData14blockIncrementEv(void*);
extern int _ZNK16GlobalConfigData10stdTNChelpEv(void*);
extern int _ZNK16GlobalConfigData17enableStraightCutEv(void*);
extern int _ZNK16GlobalConfigData10quotePathsEv(void*);
extern int _ZNK16GlobalConfigData13lineBreakModeEv(void*);
extern int _ZNK16GlobalConfigData11unitsInInchEv(void*);
extern int _ZNK16GlobalConfigData21isProgramInputModeDinEv(void*);
extern int _ZNK16GlobalConfigData18rotationalDecimalsEv(void*);
extern int _ZNK16GlobalConfigData14tchProbeActiveEv(void*);
extern int _ZNK16GlobalConfigData8darkModeEv(void*);
extern int _ZNK12GlobalNcData11startUpDoneEv(void*);
extern int _ZNK12GlobalNcData26ncChannelMainChannelNumberEv(void*);
extern int _ZNK12GlobalNcData18diagnosticsEnabledEv(void*);
extern int _ZNK12GlobalNcData20isProgrammingStationEv(void*);
extern int _ZNK12GlobalNcData22conditionalStopEnabledEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK16GlobalConfigData18comoHideStatusTabsEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK16GlobalConfigData12warningAtDELEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK16GlobalConfigData11noParaxModeEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK16GlobalConfigData10deleteBackEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK16GlobalConfigData11useProgAxesEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK16GlobalConfigData12createBackupEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK16GlobalConfigData14blockIncrementEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK16GlobalConfigData10stdTNChelpEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZNK16GlobalConfigData17enableStraightCutEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZNK16GlobalConfigData10quotePathsEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,71);int r=_ZNK16GlobalConfigData13lineBreakModeEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZNK16GlobalConfigData11unitsInInchEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,85);int r=_ZNK16GlobalConfigData21isProgramInputModeDinEv(o);int n=sprintf(buf,"%d %d\n",12,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,92);int r=_ZNK16GlobalConfigData18rotationalDecimalsEv(o);int n=sprintf(buf,"%d %d\n",13,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,99);int r=_ZNK16GlobalConfigData14tchProbeActiveEv(o);int n=sprintf(buf,"%d %d\n",14,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,106);int r=_ZNK16GlobalConfigData8darkModeEv(o);int n=sprintf(buf,"%d %d\n",15,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,113);int r=_ZNK12GlobalNcData11startUpDoneEv(o);int n=sprintf(buf,"%d %d\n",16,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,120);int r=_ZNK12GlobalNcData26ncChannelMainChannelNumberEv(o);int n=sprintf(buf,"%d %d\n",17,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,127);int r=_ZNK12GlobalNcData18diagnosticsEnabledEv(o);int n=sprintf(buf,"%d %d\n",18,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,134);int r=_ZNK12GlobalNcData20isProgrammingStationEv(o);int n=sprintf(buf,"%d %d\n",19,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,141);int r=_ZNK12GlobalNcData22conditionalStopEnabledEv(o);int n=sprintf(buf,"%d %d\n",20,r);write(1,buf,n);}
return 0;}
