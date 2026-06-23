#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
/* OptionLib::GetOptionTable(CfgControlMark const&, OptionLib::SikGeneration) */
void* _ZN9OptionLib14GetOptionTableE14CfgControlMarkNS_13SikGenerationE(void* cm, int sik){
  static void* (*real)(void*, int);
  if(!real) real = dlsym(RTLD_NEXT, "_ZN9OptionLib14GetOptionTableE14CfgControlMarkNS_13SikGenerationE");
  int controlmark = cm ? *(int*)((char*)cm + 8) : -999;
  FILE* f = fopen("/tmp/getopt.log","a");
  if(f){ fprintf(f,"GetOptionTable(controlmark=%d, sik=%d)\n", controlmark, sik); fclose(f); }
  return real ? real(cm, sik) : (void*)0;
}
