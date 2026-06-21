#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern _Bool _ZN3qic20ChannelTypeConverter12toIsEditModeENS_26ExecutionChannelController20ProgramSelectionModeE(int);
extern _Bool _ZN3qic20ChannelTypeConverter11toIsMdiModeENS_26ExecutionChannelController20ProgramSelectionModeE(int);
extern int   _ZN3qic20ChannelTypeConverter15toOperationModeE14GmTypeOfOpMode(void*);
extern int   _ZN3qic20ChannelTypeConverter11toStiBStateE24GmTypeOfNcOperationState(void*);
extern int   _ZN3qic20ChannelTypeConverter18toChannelEndResultE22GmTypeOfEndChannelCond(void*);
int main(void){
  char buf[120];
  for(int a=-1;a<=5;a++){
    int n=sprintf(buf,"e %d edit=%d mdi=%d\n", a,
      (int)_ZN3qic20ChannelTypeConverter12toIsEditModeENS_26ExecutionChannelController20ProgramSelectionModeE(a),
      (int)_ZN3qic20ChannelTypeConverter11toIsMdiModeENS_26ExecutionChannelController20ProgramSelectionModeE(a)); write(1,buf,n);
  }
  static const int V[]={0,1,7,42,-5,1000};
  for(int k=0;k<6;k++){
    unsigned char s[32] __attribute__((aligned(16))); memset(s,0,32); *(int*)(s+8)=V[k];
    int n=sprintf(buf,"p %d om=%d st=%d ce=%d\n", k,
      _ZN3qic20ChannelTypeConverter15toOperationModeE14GmTypeOfOpMode(s),
      _ZN3qic20ChannelTypeConverter11toStiBStateE24GmTypeOfNcOperationState(s),
      _ZN3qic20ChannelTypeConverter18toChannelEndResultE22GmTypeOfEndChannelCond(s)); write(1,buf,n);
  }
  return 0;
}
