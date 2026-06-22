/* rtos_probe.c — minimal heroscall ISSUER to test heros_rtos under FEX.
 * Issues Sys_getenv(0x12340027): param[0]=name, param[2]=outbuf, param[4]=size. */
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
long syscall(long n, ...);
int main(void){
    setenv("SYS", "/tmp/s", 1);                 /* heros_rtos Sys_getenv reads via getenv */
    char outbuf[256]; outbuf[0]=0;
    long param[8]; for(int i=0;i<8;i++) param[i]=0;
    param[0]=(long)"SYS"; param[2]=(long)outbuf; param[4]=256;
    long r = syscall(222, 0x12340027L, (long)param, 0L);
    printf("[rtos_probe] Sys_getenv(SYS) ret=%ld out=\"%s\"\n", r, outbuf);
    /* T_ident(self) 0x12340001 -> a task id in eax */
    long p2[8]; for(int i=0;i<8;i++) p2[i]=0;
    long tid = syscall(222, 0x12340001L, (long)p2, 0L);
    printf("[rtos_probe] T_ident(self) -> tid=%ld\n", tid);
    return 0;
}
