/* readfix.c — i386 LD_PRELOAD: make the GUEST's file reads EINTR-resilient.
 *
 * The heroscall emulator uses SIGUSR1(10)+18 as async event carriers (as_kick):
 * it tgkill()s a task's OS thread to wake it for HeROS event delivery. When such a
 * carrier lands while a guest thread is mid-read() of a regular file (e.g. winmgr
 * loading its keymap/charmap XML), read() returns -1/EINTR. Guest code that does not
 * retry EINTR then fails: IO::FileStream::Read (libxmlreader) throws Xml::Exception
 * "File read error" -> EvtExceptionShell RETRIES the FThread eval-context -> the
 * replay desyncs (index starts at the full child count) -> myChildren[] OOB SIGSEGV.
 * This is timing-dependent (whether a carrier happens to land during the read),
 * which is exactly the documented winmgr "warm good run vs cold crash" variance.
 *
 * We CANNOT just add SA_RESTART to SIGUSR1: the emulator's own ev_receive/sem_request
 * futex loops RELY on SIGUSR1 returning EINTR to re-check the event word. So instead
 * we fix only the guest's blocking file-read wrappers here. The emulator issues its
 * own I/O via raw SYS_read (bypassing this interposer), so it is unaffected. Retrying
 * EINTR on read/pread/readv is always correct for a regular file.
 *
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o readfix.so emulator/readfix.c -ldl
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/uio.h>
#include <stdio.h>

static ssize_t (*r_read)(int,void*,size_t);
static ssize_t (*r_pread)(int,void*,size_t,off_t);
static ssize_t (*r_readv)(int,const struct iovec*,int);
static size_t  (*r_fread)(void*,size_t,size_t,FILE*);

static void logfail(const char*w,int fd,int e,int retried){
    char b[112]; int n=snprintf(b,sizeof b,"[readfix] %s(fd=%d) errno=%d (eio_retries=%d)\n",w,fd,e,retried);
    (void)!write(2,b,n);
}
/* Retry EINTR (unbounded: the emulator's SIGUSR1 event-carrier interrupts syscalls)
 * AND EIO (bounded: the vz virtual disk returns transient EIO under heavy concurrent
 * FEX I/O at constellation startup — the file IS readable, the error is transient).
 * A regular-file read leaves the fd offset unchanged on failure, so re-issuing the
 * same read resumes correctly. Bounded so a genuine permanent EIO still surfaces.
 *
 * The transient-EIO window from the vz block backend can last well over the old
 * 40*200us=8ms budget (observed: Guppy's read of HwViewer.py EIO'd for ~1s+ while the
 * constellation's other FEX procs hammered the disk at startup, tripping a Python
 * tokenizer "I/O error while reading" -> the OEM script never ran -> no softkey login).
 * Use a graduated backoff up to ~6s total so a multi-second transient EIO is survived
 * while a genuinely-permanent EIO still surfaces (a startup read can afford the wait). */
#define EIO_MAX 1200
static void eio_backoff(int i){ usleep(i<64 ? 200 : 5000); }  /* first 64 fast (12.8ms), then 5ms*1136 ~= 5.7s */
ssize_t read(int fd, void *buf, size_t n){
    if(!r_read) r_read = dlsym(RTLD_NEXT,"read");
    ssize_t rc; int eio=0;
    for(;;){ rc = r_read(fd,buf,n);
        if(rc>=0) break;
        if(errno==EINTR) continue;
        if(errno==EIO && eio<EIO_MAX){ eio_backoff(eio); eio++; continue; }
        break; }
    if(rc<0) logfail("read",fd,errno,eio);
    else if(eio) logfail("read-recovered",fd,0,eio);
    return rc;
}
ssize_t pread(int fd, void *buf, size_t n, off_t off){
    if(!r_pread) r_pread = dlsym(RTLD_NEXT,"pread");
    ssize_t rc; int eio=0;
    for(;;){ rc = r_pread(fd,buf,n,off);
        if(rc>=0) break;
        if(errno==EINTR) continue;
        if(errno==EIO && eio<EIO_MAX){ eio_backoff(eio); eio++; continue; }
        break; }
    if(rc<0) logfail("pread",fd,errno,eio);
    return rc;
}
ssize_t readv(int fd, const struct iovec *iov, int c){
    if(!r_readv) r_readv = dlsym(RTLD_NEXT,"readv");
    ssize_t rc; int eio=0;
    for(;;){ rc = r_readv(fd,iov,c);
        if(rc>=0) break;
        if(errno==EINTR) continue;
        if(errno==EIO && eio<EIO_MAX){ eio_backoff(eio); eio++; continue; }
        break; }
    if(rc<0) logfail("readv",fd,errno,eio);
    return rc;
}
/* stdio path (FileStream may use FILE*): retry a short read that stopped on EINTR or
 * transient EIO. NOTE: glibc's own getc/fgets underflow reads via the internal __read
 * alias, which this public-symbol interposer does NOT catch — so Python's source-file
 * tokenizer (getc-based) is NOT covered here; the run pre-warms the page cache for the
 * Python/resource tree so those reads hit RAM (no disk I/O -> no EIO). This wrapper
 * still covers any consumer that calls fread() directly. */
size_t fread(void *buf, size_t sz, size_t nm, FILE *f){
    if(!r_fread) r_fread = dlsym(RTLD_NEXT,"fread");
    size_t got = r_fread(buf,sz,nm,f); int eio=0;
    while(got<nm && ferror(f) && (errno==EINTR || (errno==EIO && eio<EIO_MAX))){
        if(errno==EIO){ eio_backoff(eio); eio++; }
        clearerr(f);
        size_t more = r_fread((char*)buf+got*sz, sz, nm-got, f);
        if(more==0 && !ferror(f)) break;
        got += more;
    }
    return got;
}
