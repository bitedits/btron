/*
 * src/fs/fs_stdio.c — the byte-stream file API on the freestanding lanes
 *
 * include/btron/libc_shim.h states the rule this file exists to obey: on a
 * freestanding target the kernel supplies the libc *names*, and a translation
 * unit that calls one the shim does not declare is a build break to fix here
 * once rather than in every caller.  src/media/{orchestra_session,orchestra_smf,gst_rt}.c
 * read and write /SYS bodies — a .orch session, a .mid, an exported .wav — and
 * they write the operation, not the instruction: fopen/fread/fwrite/fclose and
 * stat().st_size.  So this file is the one bridge from that POSIX shape onto the
 * 2-level Record-Stream engine in src/fs/file.c, exactly as src/clu/cp.c and
 * src/clu/vfs.c already drive that engine from the CLU side.
 *
 * On a hosted build this compiles to nothing (see the #if below) and the real
 * libc answers the same calls; that is deliberate.  The DAW must reach the same
 * /SYS bytes on the Emotion Engine that it reaches on the host, which is the
 * portability claim in doc/txt/PS2.txt rather than a second filesystem.
 *
 * The mapping, and the places where it is a projection rather than an
 * identity:
 *
 *   size      A Btron file's whole byte length is FileHeader.total_size (a UW),
 *             and each record's own length is RecordIndex.size (also a UW) —
 *             the engine's in-core index is 32-bit clean even though the record
 *             calls take a `W` count.  st_size is exact for a 73408-byte session
 *             file, which is the single reason this bridge exists at all:
 *             read_orch() rejects any body whose length is not exactly
 *             ORCH_FILE_BYTES.  A 16-bit-wide answer would have said 8192 for it
 *             and refused every valid session.
 *   reading   One byte offset maps onto (record index, offset within record) by
 *             walking of->ridx[], then rd_rec() with pos_rec() for the in-record
 *             seek.  The record layout is not the reader's business: a body
 *             mkbtronfs wrote as one record and a body this file wrote as
 *             chunked records read back identically, which is what makes a host
 *             authored session playable on the target.
 *   writing   Sequential appends only.  Bytes stage into a fixed chunk and every
 *             full chunk becomes one record, because the record index has
 *             REC_IDX_LEVEL0_MAX rows, so an unbounded byte-at-a-time record per
 *             fwrite() call would exhaust the index long before the volume is
 *             full.  A seek-and-write or a hole-poking write is NOT modelled:
 *             the offset cursor is checked and a non-sequential write is refused
 *             rather than mis-answered.  Every caller in the tree appends — the
 *             WAV header then the PCM, the SMF header then the track bodies, the
 *             session as one pack_view() blob.
 *   arithmetic Every counter here is a UW, and size_t on -mabi=32 is 32 bits, so
 *             the only divisions in this file are 32-bit and the assembler's
 *             divu answers them.  That is a law, not a preference: this lane
 *             links `-nostdlib` on a 32-bit-GPR MIPS, there is no __udivdi3 to
 *             answer an `unsigned long long` division, and make test-ps2 counts
 *             named undefined symbols rather than letting them through.
 *
 * Limits worth naming instead of discovering: the volume is RDRAM-resident on
 * PS2 (core_ps2.c mounts btron_sys.vol through blk_mem_create), so a fclose()
 * after writing changes the image in RAM only — nothing survives a reboot, and
 * vol_sync() costs block writes the bench measures.  The writable body ceiling
 * is the smaller of the two budgets this bridge can see: REC_IDX_LEVEL0_MAX ×
 * FS_STDIO_CHUNK = 512 × 8192 = 4 MiB of record index, and the shipped
 * btron_sys.vol is 1 MiB of 8 KiB blocks, so the volume runs out first and
 * ins_rec() answers -5 (E_LIMIT) on 1 MiB volumes, 16 MiB on 4 MiB volumes.
 * Neither is close to the 80 MB WAV cap in include/media/gst_rt.h, so a
 * long export is a known future failure rather than a silent one: the flush
 * below returns short and the caller's own `wr != n` check reports it as
 * "truncated", which is where the tree already puts that decision.
 */

#include <stddef.h>
#include <stdint.h>

#include <btron/libc_shim.h>              /* BTRON_HOSTED, FILE, struct stat, the decls */

#if !BTRON_HOSTED

#include <btron/basic.h>
#include <btron/types.h>
#include <btron/error.h>
#include <btron/file.h>
#include <btron/fs/fs_internal.h>
#include <btron/fs/fs_types.h>
#include <btron/fs/vol_api.h>

#define FS_STDIO_SLOTS   8               /* simultaneously open FILEs */
#define FS_STDIO_CHUNK   8192u           /* staged bytes per written record */

typedef struct {
    int      used;
    int      writable;
    ID       fd;                         /* opn_fil/cre_fil slot */
    ID       rec;                        /* cached opn_rec slot, -1 when closed */
    W        rec_idx;                    /* record `rec` refers to */
    UW       rec_left;                   /* bytes still readable in `rec` */
    UW       pos;                        /* byte cursor, from body start */
    UW       size;                       /* body length at open */
    UW       wpos;                       /* bytes committed to records */
    UW       wrec;                       /* next record index to append at */
    UW       staged;                     /* bytes sitting in buf */
    UW       cap;                        /* sizeof buf */
    unsigned char *buf;
} FsStdio;

static FsStdio s_files[FS_STDIO_SLOTS];

static FsStdio *slot_take(void)
{
    int i;
    for (i = 0; i < FS_STDIO_SLOTS; i++) {
        if (!s_files[i].used) return &s_files[i];
    }
    return 0;
}

static void slot_release(FsStdio *f)
{
    if (!f) return;
    if (f->rec >= 0) { (void)cls_rec(f->rec); }
    Ifree(f->buf);
    memset(f, 0, sizeof *f);
    f->fd  = -1;
    f->rec = -1;
}

/* Close the cached record handle; the file handle stays open. */
static void rec_drop(FsStdio *f)
{
    if (f->rec >= 0) {
        (void)cls_rec(f->rec);
        f->rec = -1;
        f->rec_left = 0u;
    }
}

/*
 * Commit the whole staged buffer as one record.  The chunk is a compile-time
 * constant well inside a `W`, so no per-record length cap is invented here; the
 * failure this returns is the engine's own — a full record index or a full
 * volume — and the caller reports it as a short write.
 */
static int flush_record(FsStdio *f)
{
    ER er;
    if (f->staged == 0u) return 1;
    er = ins_rec(f->fd, (W)f->wrec, f->buf, (W)f->staged);
    if (er != E_OK) return 0;
    f->wpos   = f->wpos + f->staged;
    f->wrec   = f->wrec + 1u;
    f->staged = 0u;
    return 1;
}

/*
 * stat() — the one question the DAW actually asks is how long the body is, and
 * FileHeader.total_size answers it without reading a byte of payload.  No first
 * party caller reads st_mode (checked: S_ISREG/S_ISDIR appear nowhere outside
 * the POSIX headers), so it is reported as a plain regular file.  A drawer or a
 * volume root still cannot pass for a session body: the length check in
 * read_orch() and the cap check in orch_cmd_smf_load() are what refuse them,
 * which is where the tree already puts that decision.
 */
int stat(const char *path, struct stat *st)
{
    ID fd;
    OpenFile *of;
    if (path == 0 || st == 0) return -1;
    memset(st, 0, sizeof *st);
    fd = opn_fil(path, F_READ);
    if (fd < 0) return -1;
    of = &g_open_files[(int)fd];
    st->st_size  = (long long)of->hdr.total_size;
    st->st_mode  = S_IFREG;
    st->st_mtime = (long long)of->hdr.mtime;
    (void)cls_fil(fd);
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    FsStdio *f;
    ID fd;
    int wr = 0;
    if (path == 0 || mode == 0) return 0;
    if (mode[1] != 0) return 0;               /* "r+b" and friends are not modelled */
    if (mode[0] == 'w') wr = 1;
    else if (mode[0] != 'r') return 0;

    f = slot_take();
    if (!f) return 0;
    memset(f, 0, sizeof *f);
    f->used     = 1;
    f->fd       = -1;
    f->rec      = -1;
    f->rec_idx  = -1;
    f->writable = wr;
    f->cap      = FS_STDIO_CHUNK;
    f->buf = (unsigned char *)Imalloc(f->cap);
    if (!f->buf) { f->used = 0; return 0; }

    if (wr) {
        /* Same reset-before-create rule vfs_write_file() follows: a stale body
         * must not survive as leftover records behind the new ones. */
        (void)del_fil(path);
        fd = cre_fil(path, F_WRITE);
    } else {
        fd = opn_fil(path, F_READ);
    }
    if (fd < 0) { Ifree(f->buf); f->used = 0; return 0; }
    f->fd   = fd;
    f->size = (UW)g_open_files[(int)fd].hdr.total_size;
    return (FILE *)f;
}

/*
 * Position the cached record handle at byte `at` of the body.  The handle is
 * kept across calls because closing a record per call would thrash the 128-slot
 * record table on a 73 KB session read.
 */
static int seek_to(FsStdio *f, UW at)
{
    OpenFile *of;
    UW rec_start = 0u;
    UW r;
    of = &g_open_files[(int)f->fd];
    for (r = 0; r < of->nrec; r++) {
        UW rsz = of->ridx[r].size;
        if (at >= rec_start && at - rec_start < rsz) {
            if (f->rec < 0 || f->rec_idx != (W)r) {
                rec_drop(f);
                f->rec = opn_rec(f->fd, (W)r, F_READ);
                f->rec_idx = (W)r;
                if (f->rec < 0) return -1;
            }
            if (at != rec_start) {
                if (pos_rec(f->rec, (W)(at - rec_start), REC_POS_SET) != E_OK) return -1;
            }
            f->rec_left = rsz - (at - rec_start);
            return 0;
        }
        rec_start += rsz;
    }
    return -1;                       /* offset at or past the last record */
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream)
{
    FsStdio *f = (FsStdio *)stream;
    unsigned char *dst = (unsigned char *)ptr;
    UW want, got = 0u;
    UW sz;

    if (!stream || size == 0 || count == 0) return 0;
    sz = (UW)size;
    if (sz == 0u) return 0;
    want = sz * (UW)count;
    if (want / sz != (UW)count) return 0;          /* 32-bit product wrapped */
    if (f->writable) return 0;

    while (got < want && f->pos + got < f->size) {
        UW need = want - got;
        ER er;
        W read_back = 0;
        if (f->rec_left == 0u) {
            if (seek_to(f, f->pos + got) != 0) break;
            continue;
        }
        if (need > f->rec_left) need = f->rec_left;
        er = rd_rec(f->rec, dst + got, (W)need, &read_back);
        if (er != E_OK || read_back <= 0) break;
        got        += (UW)read_back;
        f->rec_left -= (UW)read_back;
    }
    rec_drop(f);
    f->pos += got;
    return (size_t)(got / sz);
}

/*
 * fwrite() — append only.  A caller that fseek()s backwards to patch a length
 * field would be silently wrong here, so the cursor is checked and the write
 * refused rather than mis-answered.  (Nothing in this tree does: the WAV header
 * is built in memory and written before the PCM, per gst_rt_export_wav.)
 */
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    FsStdio *f = (FsStdio *)stream;
    const unsigned char *src = (const unsigned char *)ptr;
    UW want, done = 0u;
    UW sz;

    if (!stream || size == 0 || count == 0) return 0;
    sz = (UW)size;
    if (sz == 0u) return 0;
    want = sz * (UW)count;
    if (want / sz != (UW)count) return 0;          /* 32-bit product wrapped */
    if (!f->writable) return 0;
    if (f->pos != f->wpos + f->staged) return 0;   /* not a sequential append */

    while (done < want) {
        UW room = f->cap - f->staged;
        UW take = want - done;
        UW k;
        if (take > room) take = room;
        for (k = 0; k < take; k++) f->buf[f->staged + k] = src[done + k];
        f->staged += take;
        done      += take;
        if (f->staged >= f->cap && !flush_record(f)) break;
    }
    f->pos = f->wpos + f->staged;
    return (size_t)(done / sz);
}

int fclose(FILE *stream)
{
    FsStdio *f = (FsStdio *)stream;
    int rc = 0;
    if (!f) return -1;
    if (f->writable) {
        if (f->staged > 0u) {
            if (!flush_record(f)) rc = -1;
        } else if (f->wrec == 0u) {
            /* An empty "wb" file still needs one record to exist at all. */
            if (ins_rec(f->fd, 0, "", 0) != E_OK) rc = -1;
        }
        if (rc == 0) {
            OpenFile *of = &g_open_files[(int)f->fd];
            if (of_vol(of)) vol_sync(of_vol(of));
        }
    }
    if (f->fd >= 0) { if (cls_fil(f->fd) != E_OK) rc = -1; }
    slot_release(f);
    return rc;
}

#endif /* !BTRON_HOSTED */
