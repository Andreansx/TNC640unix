/*
 * libplcbin_rebuilt.c — native re-implementation of the proprietary i386
 * libplcbin.so PLC-binary-module parser, recovered via Ghidra
 * (work/re/out/libplcbin.so.decomp.c) + i386 disassembly.
 *
 * Unlike the pure-math leaf libs, this is a *stateful file parser*: it reads a
 * big-endian "BIN PLC binary module" file, validates the 40-byte magic/version,
 * and walks a token table into a flat uint32 info struct. The opaque handle is
 * created and consumed entirely inside the library, so its internal layout need
 * not match the original (i386 4-byte vs arm64 8-byte pointers); only the parsed
 * outputs — which are uint32 arrays — are observable, and those are reproduced
 * exactly. Verified byte-identical vs the real i386 .so on crafted inputs;
 * see build_and_verify_plcbin.sh.
 *
 * Exports (plain C names, matching the original): PLCBin_Open / _Close /
 * _ReadBinCode / _ReadInfo / _ReadSPLCInfo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* opaque handle (native layout; never observed outside the library) */
typedef struct {
    FILE    *f;
    int32_t  version;     /* 200 (v2.0) or 100 (v1.0) */
    int32_t  bin_off;     /* bincode file offset  (handle[2]) */
    int32_t  bin_size;    /* bincode size         (handle[3]) */
    int32_t  bin_pos;     /* bincode read cursor  (handle[4]) */
} PlcHandle;

/* big-endian readers (the original getlong/getword byte-reverse on read) */
static int getlong(uint32_t *out, FILE *f) {
    uint32_t v;
    if (fread(&v, 4, 1, f) == 0) return 0;
    *out = v >> 24 | (v & 0xff0000) >> 8 | (v & 0xff00) << 8 | v << 24;
    return 1;
}
static int getword(uint16_t *out, FILE *f) {
    uint16_t v;
    if (fread(&v, 2, 1, f) == 0) return 0;
    *out = (uint16_t)(v << 8 | v >> 8);
    return 1;
}

/* token tables (extracted verbatim from the proprietary .so) */
typedef struct { const char *name; int sel; } Tok;
static const Tok TokenList[29] = {
    {"$SizePLCMEM$",0},{"BYTES",1},{"INPUTBYTES",2},{"OUTPUTBYTES",3},{"MARKERS",4},
    {"INPUTS",5},{"OUTPUTS",6},{"COUNTERS",7},{"TIMERS",8},{"STRINGS",9},
    {"$OffsetB$",10},{"$OffsetIB$",11},{"$OffsetOB$",12},{"$OffsetM$",13},{"$OffsetI$",14},
    {"$OffsetO$",15},{"$OffsetC$",16},{"$OffsetT$",17},{"$OffsetS$",18},{"MAXSTRINGLEN",19},
    {"REMBYTEMIN",20},{"REMBYTEMAX",21},{"REMMARKERMIN",22},{"REMMARKERMAX",23},{"$CRCSum$",24},
    {"MULERROR",25},{"DIVERROR",26},{"MODERROR",27},{"$STRUCT$",28},
};
static const Tok SPLCTokenList[17] = {
    {"$SizePLCMEM$",0},{"$CRCSum$",24},{"BYTES",1},{"MARKERS",4},{"INPUTS",5},
    {"OUTPUTS",6},{"TIMERS",8},{"$OffsetB$",10},{"$OffsetM$",13},{"$OffsetI$",14},
    {"$OffsetO$",15},{"$OffsetC$",16},{"$OffsetT$",17},{"STOPLCMCOUNT",33},{"STOPLCDCOUNT",34},
    {"SFROMPLCMCOUNT",35},{"SFROMPLCDCOUNT",36},
};

static const char MAGIC_V2[40] = "BIN PLC binary module  Version 2.0      ";
static const char MAGIC_V1[40] = "BIN PLC binary module  Version 1.0      ";

/* read bincode offset+size from 0x28 (only on first use, when bin_off==0) */
static int ReadBinCodeInfo(PlcHandle *h) {
    if (h->bin_off == 0) {
        if (fseek(h->f, 0x28, SEEK_SET) != 0) return -1;
        uint32_t a, b;
        if (!getlong(&a, h->f) || !getlong(&b, h->f)) return -2;
        h->bin_off = (int32_t)a;
        h->bin_size = (int32_t)b;
    }
    return 0;
}

void *PLCBin_Open(const char *path, int *err) {
    PlcHandle *h = calloc(1, sizeof(PlcHandle));
    if (!h) { if (err) *err = 1; return NULL; }
    h->f = fopen(path, "r+b");
    if (!h->f) { free(h); if (err) *err = 2; return NULL; }
    unsigned char hdr[48];
    if (fread(hdr, 0x28, 1, h->f) != 0) {
        if (memcmp(hdr, MAGIC_V2, 0x28) == 0) { h->version = 200; return h; }
        if (memcmp(hdr, MAGIC_V1, 0x28) == 0) { h->version = 100; return h; }
    }
    fclose(h->f); free(h);
    if (err) *err = 3;
    return NULL;
}

void PLCBin_Close(void *handle) {
    PlcHandle *h = handle;
    if (h) { fclose(h->f); free(h); }
}

size_t PLCBin_ReadBinCode(void *handle, void *buf, size_t n) {
    PlcHandle *h = handle;
    if (!h) return (size_t)-1;
    int rc = ReadBinCodeInfo(h);
    if (rc < 0) return (size_t)rc;
    size_t want;
    if (h->bin_pos == 0) {
        if (fseek(h->f, h->bin_off, SEEK_SET) != 0) return (size_t)-3;
    }
    want = (size_t)(h->bin_size - h->bin_pos);
    if ((long)n < h->bin_size - h->bin_pos) want = n;
    if (want != 0) {
        h->bin_pos += (int32_t)want;
        return fread(buf, 1, want, h->f);
    }
    h->bin_off = h->bin_size = h->bin_pos = 0;
    return 0;
}

/* walk the token section at 0x48 -> info table; common to ReadInfo/ReadSPLCInfo */
static int parse_tokens(PlcHandle *h, uint32_t *info,
                        const Tok *toks, int ntok,
                        void (*apply)(uint32_t *info, int sel, uint32_t val)) {
    int rc = ReadBinCodeInfo(h);
    if (rc != 0) return rc;                    /* propagate handle err (-1/-2) */
    info[1] = (uint32_t)h->bin_size;
    if (fseek(h->f, 0x48, SEEK_SET) != 0) return -3;
    uint32_t tbl_off, tbl_len;
    if (!getlong(&tbl_off, h->f) || !getlong(&tbl_len, h->f)) return -3;
    if (fseek(h->f, (long)tbl_off, SEEK_SET) != 0) return -3;
    for (int pos = 0; pos < (int)tbl_len; pos += 0x16) {
        char name[24];
        if (fread(name, 0x10, 1, h->f) == 0) return -3;
        name[16] = 0;                         /* ensure null-terminated for strcmp */
        uint16_t zero; uint32_t val;
        if (!getword(&zero, h->f)) return -3;
        if (zero != 0) return -3;
        if (!getlong(&val, h->f)) return -3;
        for (int i = 0; i < ntok; i++) {
            if (strcmp(toks[i].name, name) == 0) { apply(info, toks[i].sel, val); break; }
        }
    }
    return 0;
}

static void apply_info(uint32_t *p, int sel, uint32_t v) {
    switch (sel) {
    case 0:  p[0]=v; break;     case 1:  p[0xc]=v; break;  case 2:  p[0xd]=v; break;
    case 3:  p[0xe]=v; break;   case 4:  p[0xf]=v; break;  case 5:  p[0x10]=v; break;
    case 6:  p[0x11]=v; break;  case 7:  p[0x12]=v; break; case 8:  p[0x13]=v; break;
    case 9:  p[0x14]=v; break;  case 10: p[3]=v; break;    case 11: p[4]=v; break;
    case 12: p[5]=v; break;     case 13: p[6]=v; break;    case 14: p[7]=v; break;
    case 15: p[8]=v; break;     case 16: p[9]=v; break;    case 17: p[0xa]=v; break;
    case 18: p[0xb]=v; break;   case 19: p[0x15]=v; break; case 20: p[0x16]=v; break;
    case 21: p[0x17]=v; break;  case 22: p[0x18]=v; break; case 23: p[0x19]=v; break;
    case 24: p[2]=v; break;     case 25: p[0x1b]=v; break; case 26: p[0x1a]=v; break;
    case 27: p[0x1c]=v; break;  case 28: p[0x1d]=v; break;
    }
}

int PLCBin_ReadInfo(void *handle, void *info_out) {
    PlcHandle *h = handle;
    if (!h) return -1;
    uint32_t *info = info_out;
    memset(info, 0, 0x78);                    /* 30 uint32 */
    return parse_tokens(h, info, TokenList, 29, apply_info);
}

static void apply_splc(uint32_t *p, int sel, uint32_t v) {
    switch (sel) {
    case 0:  p[0]=v; break;     case 1:  p[0xc]=v>>2; break; case 4:  p[0xd]=v; break;
    case 5:  p[0xe]=v; break;   case 6:  p[0xf]=v; break;    case 8:  p[0x10]=v; break;
    case 10: p[3]=v; break;     case 13: p[4]=v; break;      case 14: p[5]=v; break;
    case 15: p[6]=v; break;     case 17: p[7]=v; break;      case 24: p[2]=v; break;
    case 33: p[0x11]=v; break;  case 34: p[0x12]=v; break;   case 35: p[0x13]=v; break;
    case 36: p[0x14]=v; break;
    }
}

int PLCBin_ReadSPLCInfo(void *handle, void *info_out) {
    PlcHandle *h = handle;
    if (!h) return -1;
    uint32_t *p = info_out;
    memset(p, 0, 0x54);                        /* 21 uint32 */
    int rc = parse_tokens(h, p, SPLCTokenList, 17, apply_splc);
    if (rc != 0) return rc;
    uint32_t u = p[3] + p[0xc] * 4 - p[0x14] * 4;
    p[0xb] = u;
    p[9] = u - p[0x12] * 4;
    u = (p[0xd] + p[4]) - p[0x13];
    p[10] = u;
    p[8] = u - p[0x11];
    return 0;
}
