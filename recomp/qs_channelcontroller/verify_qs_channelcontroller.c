#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}
extern int _ZNK3qic17ChannelController10hasControlEv(void*);
extern int _ZNK3qic17ChannelController17lastUsedRequestIdEv(void*);
extern int _ZNK3qic17ChannelController16connectionHandleEv(void*);
extern int _ZNK3qic20GeoServerReplyObject8mnemonicEv(void*);
extern int _ZNK3qic25NcQParamReadRequestObject7isReadyEv(void*);
extern int _ZNK3qic25NcQParamReadRequestObject14maxNumNumericsEv(void*);
extern int _ZNK3qic25NcQParamReadRequestObject12maxNumLocalsEv(void*);
extern int _ZNK3qic25NcQParamReadRequestObject15maxNumRemanentsEv(void*);
extern int _ZNK3qic25NcQParamReadRequestObject13maxNumStringsEv(void*);
extern int _ZNK3qic24QParamWriteRequestObject15preparedMessageEv(void*);
extern int _ZNK3qic29CycleQParamWriteRequestObject15preparedMessageEv(void*);
extern int _ZNK3qic20ToolCheckReplyObject10returnCodeEv(void*);
int main(void){char buf[64];
{unsigned char o[1024];fill(o,1024,1);int r=_ZNK3qic17ChannelController10hasControlEv(o);int n=sprintf(buf,"%d %d\n",0,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,8);int r=_ZNK3qic17ChannelController17lastUsedRequestIdEv(o);int n=sprintf(buf,"%d %d\n",1,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,15);int r=_ZNK3qic17ChannelController16connectionHandleEv(o);int n=sprintf(buf,"%d %d\n",2,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,22);int r=_ZNK3qic20GeoServerReplyObject8mnemonicEv(o);int n=sprintf(buf,"%d %d\n",3,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,29);int r=_ZNK3qic25NcQParamReadRequestObject7isReadyEv(o);int n=sprintf(buf,"%d %d\n",4,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,36);int r=_ZNK3qic25NcQParamReadRequestObject14maxNumNumericsEv(o);int n=sprintf(buf,"%d %d\n",5,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,43);int r=_ZNK3qic25NcQParamReadRequestObject12maxNumLocalsEv(o);int n=sprintf(buf,"%d %d\n",6,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,50);int r=_ZNK3qic25NcQParamReadRequestObject15maxNumRemanentsEv(o);int n=sprintf(buf,"%d %d\n",7,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,57);int r=_ZNK3qic25NcQParamReadRequestObject13maxNumStringsEv(o);int n=sprintf(buf,"%d %d\n",8,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,64);int r=_ZNK3qic24QParamWriteRequestObject15preparedMessageEv(o);int n=sprintf(buf,"%d %d\n",9,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,71);int r=_ZNK3qic29CycleQParamWriteRequestObject15preparedMessageEv(o);int n=sprintf(buf,"%d %d\n",10,r);write(1,buf,n);}
{unsigned char o[1024];fill(o,1024,78);int r=_ZNK3qic20ToolCheckReplyObject10returnCodeEv(o);int n=sprintf(buf,"%d %d\n",11,r);write(1,buf,n);}
return 0;}
