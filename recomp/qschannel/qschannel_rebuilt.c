/* libQsChannelController — 5 ChannelTypeConverter enum/state converters.
 * Decompiled with IDA 9.2 off genuine i386 libQsChannelController.so. NC channel
 * mode/state mapping. toIs*Mode are pure (enum==const); the to* converters read
 * field@8 of the Gm* message object passed by (hidden) pointer. C++ mangled. */
_Bool _ZN3qic20ChannelTypeConverter12toIsEditModeENS_26ExecutionChannelController20ProgramSelectionModeE(int a1){ return a1 == 0; }
_Bool _ZN3qic20ChannelTypeConverter11toIsMdiModeENS_26ExecutionChannelController20ProgramSelectionModeE(int a1){ return a1 == 1; }
int   _ZN3qic20ChannelTypeConverter15toOperationModeE14GmTypeOfOpMode(void *a1){ return *(int*)((char*)a1+8); }
int   _ZN3qic20ChannelTypeConverter11toStiBStateE24GmTypeOfNcOperationState(void *a1){ return *(int*)((char*)a1+8); }
int   _ZN3qic20ChannelTypeConverter18toChannelEndResultE22GmTypeOfEndChannelCond(void *a1){ return *(int*)((char*)a1+8); }
