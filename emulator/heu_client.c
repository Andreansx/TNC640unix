/* heu_client.c — minimal REAL libheuseradmin client to validate the heuserver link.
 * dlopen()s libheuseradmin at RUNTIME (so the build doesn't link the control's old glibc;
 * FEX resolves the closure from the rootfs at run time) and calls HEUTicketFromPid(getpid()):
 * the client lib connects to heuserver (127.0.0.1:19093), sends a HEUTicketFromPid request,
 * heuserver identifies the peer + grants/denies a ticket. Proves real heros client code
 * (not a raw TCP probe) connects + handshakes end-to-end.
 * Build (i386, modern host glibc): i686-linux-gnu-gcc -O2 heu_client.c -o heu_client -ldl */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>

int main(void) {
    int mypid = (int)getpid();
    void *h = dlopen("libheuseradmin.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "[heu_client] dlopen failed: %s\n", dlerror()); return 3; }
    void *(*ticketFromPid)(int) = (void *(*)(int))dlsym(h, "HEUTicketFromPid");
    void  (*ticketFree)(void *) = (void  (*)(void *))dlsym(h, "HEUTicketFree");
    if (!ticketFromPid) { fprintf(stderr, "[heu_client] no HEUTicketFromPid: %s\n", dlerror()); return 4; }
    fprintf(stderr, "[heu_client] pid=%d calling HEUTicketFromPid(%d)...\n", mypid, mypid);
    void *t = ticketFromPid(mypid);
    fprintf(stderr, "[heu_client] HEUTicketFromPid -> %p (%s)\n",
            t, t ? "GOT TICKET (heuserver answered)" : "NULL (denied / no answer)");
    if (t && ticketFree) ticketFree(t);
    return t ? 0 : 2;
}
