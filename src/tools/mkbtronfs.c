/*
 * mkbtronfs — B-System BTRON volume image builder
 *
 * Usage:
 *   mkbtronfs manifest.txt [-o output.vol] [-n NFMAX] [-b NLB]
 *
 * Manifest format (one entry per line, '#' comments allowed):
 *   DIR   "name"   parent_container|-
 *   FILE  "name"   TAD   [payload_file_path|-]   [parent_container]
 *   FILE  "name"   MD|TXT|PNG|IMG|BIN  payload_file_path   [parent_container]
 *   FILE  "name"   LINK  [target_fid|-]          [parent_container]
 *
 * A token is either a bare word or a "quoted string" that may hold spaces; the quoted form is
 * what lets a file named `Dynamite Go! Go! - Demo (2021) (Rutubo Gameworks).ROM` be described at all.
 *
 * TAD, MD, TXT, PNG and IMG records are typed RT_TADDATA because a reader is meant to decode
 * them; BIN is typed RT_DATABOX because its bytes are opaque to the filesystem -- a cartridge
 * image read as text is a screenful of noise, and a body typed DATABOX is one the text viewers
 * of this system decline to interpret.
 *
 * Defaults: NFMAX=256, NLB=1024 (1 MiB image), output=btron_sys.vol
 *
 * This is a standalone POSIX host tool.  It includes the FS library
 * sources via direct linking (see Makefile target).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>       /* time_t for the stamp, and for SOURCE_DATE_EPOCH */
#include <sys/stat.h>   /* stat(), for the newest-input-mtime rule below */

/* We need the FS API; include indirectly via the public headers */
#include <btron/fs/block.h>
#include <btron/fs/vol_api.h>
#include <btron/file.h>
#include <btron/tad.h>
#include <btron/fs/fs_internal.h>

/* Default geometry */
#define DEFAULT_NFMAX  256
#define DEFAULT_NLB   1024
#define DEFAULT_OUTPUT "btron_sys.vol"
#define DEFAULT_VOLNAME "SYS"

/* ── Simple TAD helper: build a TS_TEXT segment from ASCII bytes ── */
static unsigned char *make_tad_text(const char *text, size_t *out_len)
{
    size_t tlen = strlen(text);
    /* TAD_SEG_HDR: 0xFF 0xE1 <len16be> + text bytes */
    size_t total = 4 + tlen;
    unsigned char *buf = (unsigned char *)malloc(total);
    if (!buf) return NULL;
    buf[0] = 0xFF;
    buf[1] = 0xE1; /* TS_TEXT */
    buf[2] = (unsigned char)(tlen >> 8);
    buf[3] = (unsigned char)(tlen);
    memcpy(buf + 4, text, tlen);
    *out_len = total;
    return buf;
}

static char g_vol_name[40] = DEFAULT_VOLNAME;

/* ── One manifest token reader ────────────────────────────────────
 *
 * The manifest is read twice: once to build the volume and once to work out the stamp the
 * volume carries (newest_input_mtime below).  Both passes have to agree on which bytes of a
 * line name a file, or editing a path could change the image without changing any of its
 * documents.  So there is one reader here and no second loop over the same lines anywhere.
 *
 * A token is a bare word up to whitespace, or a "double-quoted" run that may contain spaces --
 * which is what makes a cartridge named `Dynamite Go! Go! - Demo (2021) (Rutubo Gameworks).ROM` describable.  Whitespace
 * after the token is consumed as the separator.  Returns NULL at end of line; a token longer than
 * `cap` is truncated rather than rejected, as it always was. */
static char *next_token(char **pp, char *out, size_t cap)
{
    char *p = *pp;
    size_t i = 0;

    if (cap == 0) return NULL;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) { *pp = p; out[0] = '\0'; return NULL; }

    /* A token longer than `cap` is truncated but still consumed to its end: leaving the reader
     * parked in the middle of an over-long path would make every later token on the line wrong,
     * which is a worse failure than one name that does not match the file. */
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (i + 1 < cap) out[i++] = *p++; else p++; }
        out[i] = '\0';
        if (*p == '"') p++;
    } else {
        /* `*p > ' '` is "any whitespace": it ends an unquoted token at the line's '\n' as well
         * as at a space, and a path that kept the newline is a path that does not exist. */
        while (*p && *p > ' ') { if (i + 1 < cap) out[i++] = *p++; else p++; }
        out[i] = '\0';
    }

    while (*p == ' ' || *p == '\t') p++;
    *pp = p;
    return out;
}

/* ── Is there room for this payload? ──────────────────────────────
 *
 * The volume's free-block count is the only budget a payload has to clear, and the answer is
 * worth naming up front: when a 4 MiB MegaROM is added to a 1 MiB image, ins_rec() fails deep
 * inside an extent allocation and returns a bare -1, which reads as a broken tool rather than
 * as a full disc.  One header block plus the data extent is what a body costs. */
static int room_for(const char *entry, size_t len)
{
    UW bsize, free_blocks, need;

    if (!g_sys_vol) return 1;
    bsize = vol_block_size(g_sys_vol);
    if (bsize == 0) return 1;
    need = (UW)((len + bsize - 1) / bsize) + 1u;
    free_blocks = vol_free_blocks(g_sys_vol);
    if (need <= free_blocks) return 1;

    fprintf(stderr, "mkbtronfs: '%s' needs %u of the %u free %u-byte blocks; "
                    "grow the image (-b NLB) or drop an entry\n",
            entry, need, free_blocks, bsize);
    return 0;
}

/* ── Insert RT_LINK record into parent container ──────────── */
static void add_container_link(const char *container, const char *name, FID fid)
{
    const char *parent = (container && container[0] && strcmp(container, "-") != 0) ? container : g_vol_name;
    ID parent_fd = opn_fil(parent, 0x0002 /* F_WRITE */);
    if (parent_fd < 0) {
        fprintf(stderr, "mkbtronfs: warning: parent container '%s' not found for '%s'\n", parent, name);
        return;
    }

    OpenFile *pof = &g_open_files[(int)parent_fd];
    if (pof->hdr.did == 0) {
        pof->hdr.did = (UW)pof->fid + 1;
        pof->dirty = 1;
    }
    UW parent_did = pof->hdr.did;

    /* Update child's pdid to parent_did */
    ID child_fd = opn_fil_fid(pof->vol, fid, 0x0002 /* F_WRITE */);
    if (child_fd >= 0) {
        g_open_files[(int)child_fd].hdr.pdid = parent_did;
        g_open_files[(int)child_fd].dirty = 1;
        cls_fil(child_fd);
    }

    unsigned char payload[16 + 40];
    memset(payload, 0, sizeof(payload));
    payload[0] = (unsigned char)(fid >> 24);
    payload[1] = (unsigned char)(fid >> 16);
    payload[2] = (unsigned char)(fid >> 8);
    payload[3] = (unsigned char)(fid);
    unsigned int nlen = (unsigned int)strlen(name);
    if (nlen > 39) nlen = 39;
    payload[14] = (unsigned char)(nlen >> 8);
    payload[15] = (unsigned char)(nlen);
    memcpy(payload + 16, name, nlen);

    int rec_idx = (int)pof->nrec;
    ER err = ins_rec(parent_fd, rec_idx, payload, (int)(16 + nlen));
    if (err == 0) {
        fil_set_rec_type(parent_fd, rec_idx, (UH)RT_LINK);
        pof->ridx[rec_idx].kind = 0x8000;
        pof->dirty = 1;
    }
    cls_fil(parent_fd);
}

static int imprint_dir(const char *dir_name, const char *parent_container)
{
    ID fd = cre_fil(dir_name, 0x0002 /* F_WRITE */);
    if (fd < 0) {
        fprintf(stderr, "mkbtronfs: cre_fil dir '%s' failed\n", dir_name);
        return -1;
    }
    OpenFile *of = &g_open_files[(int)fd];
    FID fid = of->fid;
    of->hdr.did = (UW)fid + 1;
    of->dirty = 1;
    cls_fil(fd);
    add_container_link(parent_container, dir_name, fid);
    return 0;
}

/* ── Read a whole file, byte-exact ──────────────────────────────── */
static unsigned char *read_whole_file(const char *path, size_t *out_len)
{
    FILE *pf;
    long fsz;
    unsigned char *buf = NULL;
    size_t got = 0;

    *out_len = 0;
    pf = fopen(path, "rb");
    if (!pf) {
        fprintf(stderr, "mkbtronfs: warning: cannot open payload '%s': %s\n",
                path, strerror(errno));
        return NULL;
    }
    fseek(pf, 0, SEEK_END);
    fsz = ftell(pf);
    rewind(pf);
    if (fsz > 0) {
        buf = (unsigned char *)malloc((size_t)fsz);
        if (!buf) {
            fprintf(stderr, "mkbtronfs: malloc failed for '%s'\n", path);
            fclose(pf);
            return NULL;
        }
        got = fread(buf, 1, (size_t)fsz, pf);
    }
    fclose(pf);
    *out_len = got;
    return buf;
}

/* ── Imprint one Real Body ─────────────────────────────────────────
 *
 * TAD, MD, TXT, PNG, IMG and BIN are one operation: put a file's bytes into record 0 of the body
 * the manifest names.  The read and the write are byte-exact -- no line ending, encoding or
 * length is interpreted -- because the same code that carries a Markdown document carries a
 * 32 KiB cartridge image, where a byte that changes is a game that does not boot.
 *
 * Two things differ per entry, and both are arguments rather than copies of this function:
 *   is_tad     -- a TAD with no payload is not an error, it is an empty document: a TS_TEXT that
 *                 names the body, which is what every placeholder TAD on /SYS has always been.
 *   rec_type   -- RT_TADDATA for a body a reader should decode, RT_DATABOX for a body whose bytes
 *                 are opaque to the filesystem.  The type is not decoration: tad_is_tad_record()
 *                 is how the text viewers of this system decide to interpret a record, and a
 *                 cartridge shown as text is a screenful of noise. */
static int imprint_body(const char *vol_name_entry, const char *payload_path,
                        const char *parent_container, int is_tad, UH rec_type)
{
    unsigned char *payload = NULL;
    size_t payload_len = 0;
    ID fd;
    FID fid;
    ER err;

    if (payload_path && payload_path[0] && strcmp(payload_path, "-") != 0) {
        payload = read_whole_file(payload_path, &payload_len);
    } else if (!is_tad) {
        fprintf(stderr, "mkbtronfs: error: entry '%s' of type %s requires a payload file\n",
                vol_name_entry, is_tad ? "TAD" : rec_type == RT_DATABOX ? "BIN" : "text");
        return -1;
    }

    if (!payload) {
        payload = make_tad_text(vol_name_entry, &payload_len);
        if (!payload) return -1;
    }

    if (!room_for(vol_name_entry, payload_len)) {
        free(payload);
        return -1;
    }

    fd = cre_fil(vol_name_entry, 0x0002 /* F_WRITE */);
    if (fd < 0) {
        fprintf(stderr, "mkbtronfs: cre_fil('%s') failed\n", vol_name_entry);
        free(payload);
        return -1;
    }
    fid = g_open_files[(int)fd].fid;

    err = ins_rec(fd, 0, payload, (int)payload_len);
    if (err == 0) fil_set_rec_type(fd, 0, rec_type);

    cls_fil(fd);
    free(payload);

    if (err != 0) {
        fprintf(stderr, "mkbtronfs: ins_rec('%s', %u bytes) failed (%d)\n",
                vol_name_entry, (unsigned)payload_len, (int)err);
        return -1;
    }
    add_container_link(parent_container, vol_name_entry, fid);
    return 0;
}

/* ── Imprint one LINK file ──────────────────────────────────────── */
static int imprint_link(const char *link_name, int target_fid_num, const char *parent_container)
{
    FS_LINK target;
    memset(&target, 0, sizeof(target));
    target.target_fid = (unsigned int)target_fid_num;

    ER err = cre_lnk(link_name, &target);
    if (err != 0) {
        fprintf(stderr, "mkbtronfs: cre_lnk('%s' -> FID%d) failed\n",
                link_name, target_fid_num);
        return -1;
    }

    ID fd = opn_fil(link_name, 0x0001);
    if (fd >= 0) {
        FID fid = g_open_files[(int)fd].fid;
        cls_fil(fd);
        add_container_link(parent_container, link_name, fid);
    }
    return 0;
}

/* ── The one manifest reader ───────────────────────────────────────
 *
 * This file is read three times: once to build the volume, once to work out the stamp the volume
 * carries (its newest input's date), and once to name the files make has to wait for.  Each used
 * to have its own idea of which bytes of a line are a path, and they had drifted -- the stamp's
 * loop kept the line's '\n' inside the payload it stat()ed, so every payload looked missing and
 * the volume was stamped from the manifest alone, while the build read the same line correctly.
 * There is one reader now, and a pass over the manifest is a callback per entry.
 *
 * `keyword` is DIR or FILE; a comment or a blank line calls nothing at all.  For DIR, `ftype`
 * and `payload` come back empty.  A truncated token is consumed to its end, so a long line
 * cannot shift the fields that follow it. */
typedef void (*manifest_cb)(const char *keyword, const char *name, const char *ftype,
                            const char *payload, const char *parent, void *ctx);

static int read_manifest(const char *manifest_path, manifest_cb cb, void *ctx)
{
    FILE *mf = fopen(manifest_path, "r");
    char line[512];

    if (!mf) {
        fprintf(stderr, "mkbtronfs: cannot open manifest '%s': %s\n",
                manifest_path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), mf)) {
        char *p = line;
        char kw[16] = {0}, name[80] = {0}, ftype[16] = {0};
        char payload[256] = {0}, parent[80] = {0};

        /* Strip the line ending so a quoted token cannot keep a '\n' inside it. */
        size_t ln = strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) line[--ln] = '\0';

        /* A blank line yields no token; a comment's first token starts with '#'. */
        if (!next_token(&p, kw, sizeof(kw)) || kw[0] == '#') continue;

        if (strcmp(kw, "DIR") == 0) {
            next_token(&p, name, sizeof(name));
            next_token(&p, parent, sizeof(parent));
            if (name[0]) cb("DIR", name, "", "", parent, ctx);
            continue;
        }
        if (strcmp(kw, "FILE") != 0) continue;

        next_token(&p, name, sizeof(name));
        next_token(&p, ftype, sizeof(ftype));
        next_token(&p, payload, sizeof(payload));
        next_token(&p, parent, sizeof(parent));
        if (name[0] && ftype[0]) cb("FILE", name, ftype, payload, parent, ctx);
    }
    fclose(mf);
    return 0;
}

/* ── Build pass: one entry, one body ─────────────────────────────── */
typedef struct { int errors; } build_ctx;

static void build_entry(const char *keyword, const char *name, const char *ftype,
                        const char *payload, const char *parent_tok, void *ctx)
{
    build_ctx *b = (build_ctx *)ctx;
    const char *parent = (parent_tok && parent_tok[0]) ? parent_tok : NULL;
    int failed = 0;

    if (strcmp(keyword, "DIR") == 0) {
        printf("  [DIR ] %s (in %s)\n", name,
               parent && strcmp(parent, "-") != 0 ? parent : g_vol_name);
        failed = imprint_dir(name, parent) != 0;
        if (failed) b->errors++;
        return;
    }

    if (strcmp(ftype, "TAD") == 0) {
        printf("  [TAD ] %s\n", name);
        failed = imprint_body(name, payload, parent, 1, RT_TADDATA) != 0;
    } else if (strcmp(ftype, "BIN") == 0) {
        /* A raw binary body: named on the volume exactly as it is on the host, and typed
         * RT_DATABOX so no reader mistakes it for a document. */
        printf("  [BIN ] %s (%s) [in %s]\n", name, payload, parent ? parent : g_vol_name);
        failed = imprint_body(name, payload, parent, 0, RT_DATABOX) != 0;
    } else if (strcmp(ftype, "MD") == 0 || strcmp(ftype, "TXT") == 0 ||
               strcmp(ftype, "PNG") == 0 || strcmp(ftype, "IMG") == 0) {
        printf("  [%-4s] %s (%s) [in %s]\n", ftype, name, payload, parent ? parent : g_vol_name);
        failed = imprint_body(name, payload, parent, 0, RT_TADDATA) != 0;
    } else if (strcmp(ftype, "LINK") == 0) {
        int target_fid = 0; /* default: root */
        if (payload[0] && payload[0] != '/') target_fid = atoi(payload);
        printf("  [LINK] %s -> FID%d\n", name, target_fid);
        failed = imprint_link(name, target_fid, parent) != 0;
    } else {
        fprintf(stderr, "mkbtronfs: unknown type '%s' for '%s'\n", ftype, name);
        failed = 1;
    }
    if (failed) b->errors++;
}

static int process_manifest(const char *manifest_path)
{
    build_ctx b = { 0 };

    if (read_manifest(manifest_path, build_entry, &b) != 0) return -1;
    return b.errors ? -1 : 0;
}

/* ── Reproducible stamp ─────────────────────────────────────────────
 *
 * The volume's file headers carry a date, and until now that date was `time(NULL)`, which made
 * every run of this tool write different bytes for the same manifest and the same documents.
 * That is cheap when the volume is a file you boot from and expensive when it is `.incbin`'d into
 * an ELF -- the PS2 image carries this payload, so a rebuild that changes nothing but a stamp
 * changes the shipped image, and the row that proves the image holds the bytes the tool wrote
 * (scripts/test_ps2.sh) compares two generations of it and goes red on the sixteenth second.
 *
 * So the stamp comes from the inputs: the newest mtime among the manifest and the payload files it
 * names, which is exactly the date the volume's contents were last true.  SOURCE_DATE_EPOCH wins
 * when a build sets it, because that is the convention other reproducible tools read and a
 * checkout whose mtimes were reset still has to produce the same image. */
static void stat_payload(const char *keyword, const char *name, const char *ftype,
                         const char *payload, const char *parent, void *ctx)
{
    time_t *newest = (time_t *)ctx;
    struct stat st;

    (void)keyword; (void)name; (void)parent;
    /* The walker already dropped the entries that name no file, but a LINK's `payload` is a FID
     * and a placeholder is '-', so the type is checked here too rather than trusted upstream. */
    if (!payload[0] || payload[0] == '-' || strcmp(ftype, "LINK") == 0) return;
    if (stat(payload, &st) == 0 && st.st_mtime > *newest) *newest = st.st_mtime;
}

static time_t newest_input_mtime(const char *manifest_path)
{
    time_t newest = 0;
    struct stat st;

    if (stat(manifest_path, &st) == 0 && st.st_mtime > newest) newest = st.st_mtime;
    read_manifest(manifest_path, stat_payload, &newest);
    return newest;
}

/* ── Make dependencies ─────────────────────────────────────────────
 *
 * The volume is a function of its payloads, so a build that edits one of them has to rebuild it.
 * The Makefile used to work that out for itself with an awk pass over the manifest -- a second
 * grammar for the same file, which could not see a quoted path, took the last field of a line as
 * the payload (wrong for every entry that names a parent drawer), and silently produced a shorter
 * list instead of an error.  It is the tool's job: this writes the same rule `-MMD` writes for a
 * compiler, and make is told to include it.
 *
 * Spaces are escaped with a backslash because that is how make spells one word, and every path
 * gets its own line with a backslash continuation -- the shape `-MMD` writes, and the only shape
 * make reads: a list whose last line has no continuation is a rule, and the line after it is a
 * recipe that no tab started ("missing separator").  A payload that does not exist is still
 * listed, because make's answer to a missing prerequisite is an error, whereas a missing
 * dependency line is a stale image nobody notices. */
#define DEP_MAX 512

typedef struct {
    char path[256];
} dep_entry;

typedef struct {
    dep_entry e[DEP_MAX];
    int n;
    int overflowed;
} dep_list;

static void collect_payload(const char *keyword, const char *name, const char *ftype,
                            const char *payload, const char *parent, void *ctx)
{
    dep_list *d = (dep_list *)ctx;

    (void)keyword; (void)name; (void)parent;
    if (!payload[0] || payload[0] == '-' || strcmp(ftype, "LINK") == 0) return;
    if (d->n >= DEP_MAX) { d->overflowed = 1; return; }
    snprintf(d->e[d->n].path, sizeof(d->e[0].path), "%s", payload);
    d->n++;
}

static void print_dep_word(FILE *out, const char *path)
{
    const char *p;

    for (p = path; *p; p++) {
        /* The six characters make reads as syntax rather than as part of a file name. */
        if (*p == ' ' || *p == '\t' || *p == '#' || *p == '$' || *p == ':' || *p == '\\') {
            fputc('\\', out);
        }
        fputc(*p, out);
    }
}

static int write_deps_file(const char *manifest_path, const char *output, const char *deps_path)
{
    static dep_list d;                    /* 128 KiB of table, and not on the stack */
    FILE *out;
    int i;

    d.n = 0;
    d.overflowed = 0;
    read_manifest(manifest_path, collect_payload, &d);

    out = fopen(deps_path, "w");
    if (!out) {
        fprintf(stderr, "mkbtronfs: cannot write dependencies '%s': %s\n", deps_path, strerror(errno));
        return -1;
    }
    fprintf(out, "%s: \\\n  ", output);
    print_dep_word(out, manifest_path);
    for (i = 0; i < d.n; i++) {
        fprintf(out, " \\\n  ");
        print_dep_word(out, d.e[i].path);
    }
    fputc('\n', out);
    fclose(out);

    if (d.overflowed) {
        fprintf(stderr, "mkbtronfs: WARNING: more than %d payloads in '%s', so the dependency list "
                        "is short and a later edit of an unlisted file will not rebuild the volume\n",
                DEP_MAX, manifest_path);
    }
    return 0;
}

/* ── main ────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    const char *manifest = NULL;
    const char *output   = DEFAULT_OUTPUT;
    const char *label    = NULL;
    const char *deps     = NULL;
    unsigned int nfmax   = DEFAULT_NFMAX;
    unsigned int nlb     = DEFAULT_NLB;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            nfmax = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            nlb = (unsigned int)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--label") == 0) && i + 1 < argc) {
            label = argv[++i];
        } else if (strcmp(argv[i], "--deps") == 0 && i + 1 < argc) {
            deps = argv[++i];
        } else if (argv[i][0] != '-') {
            manifest = argv[i];
        } else {
            fprintf(stderr, "mkbtronfs: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!manifest) {
        fprintf(stderr, "Usage: mkbtronfs manifest.txt [-o out.vol] [-n NFMAX] [-b NLB]"
                        " [-l VOLNAME] [--deps out.vol.d]\n");
        return 1;
    }

    if (label) {
        strncpy(g_vol_name, label, sizeof(g_vol_name) - 1);
    } else if (strstr(output, "anders") != NULL) {
        strncpy(g_vol_name, "ANDERS", sizeof(g_vol_name) - 1);
    } else {
        strncpy(g_vol_name, DEFAULT_VOLNAME, sizeof(g_vol_name) - 1);
    }

    printf("mkbtronfs: formatting '%s' (Label: %s)  NFMAX=%u NLB=%u (%u KiB)\n",
           output, g_vol_name, nfmax, nlb, nlb);

    /* Pin the stamp before the first header is written, so the bytes below depend on the inputs
     * and not on the clock.  A stamp that cannot be read from the inputs -- a checkout whose mtimes
     * were reset before 2000-01-01, which is what a plain `tar -x` gives -- leaves the tool on the
     * clock, so say so on stderr rather than let a non-deterministic image look reproducible. */
    {
        const char *epoch = getenv("SOURCE_DATE_EPOCH");
        time_t t = epoch ? (time_t)strtol(epoch, NULL, 10) : newest_input_mtime(manifest);
        if (t > 946684800L) {
            btron_fs_set_stamp((UW)(t - 946684800L));
            printf("mkbtronfs: stamp %s = %ld (secs since 2000-01-01: %u)\n",
                   epoch ? "SOURCE_DATE_EPOCH" : "newest input mtime", (long)t,
                   (unsigned)(t - 946684800L));
        } else {
            fprintf(stderr, "mkbtronfs: WARNING: no usable stamp from %s (%ld); falling back to the "
                            "clock, so this volume is NOT reproducible.  Set SOURCE_DATE_EPOCH.\n",
                    epoch ? "SOURCE_DATE_EPOCH" : "input mtimes", (long)t);
        }
    }

    /* The dependency file is written before the volume, so a build that fails on a missing or
     * oversized payload still leaves make knowing what the next attempt has to wait for. */
    if (deps) (void)write_deps_file(manifest, output, deps);

    /* Create and format the volume */
    BlkDev *dev = blk_file_create(output, 1 /* create_new */, (unsigned int)nlb);
    if (!dev) {
        fprintf(stderr, "mkbtronfs: cannot create output file '%s'\n", output);
        return 1;
    }

    if (vol_format(dev, (unsigned int)nfmax, (unsigned int)nlb, g_vol_name) != 0) {
        fprintf(stderr, "mkbtronfs: vol_format failed\n");
        blk_file_close(dev);
        return 1;
    }

    g_sys_vol = vol_mount(dev);
    if (!g_sys_vol) {
        fprintf(stderr, "mkbtronfs: vol_mount failed (bad magic after format)\n");
        blk_file_close(dev);
        return 1;
    }

    /* Guarantee root container has did = 1 and pdid = 0 */
    ID rfd = opn_fil_fid(g_sys_vol, FID_ROOT, 0x0002);
    if (rfd >= 0) {
        g_open_files[(int)rfd].hdr.did = 1;
        g_open_files[(int)rfd].hdr.pdid = 0;
        g_open_files[(int)rfd].dirty = 1;
        cls_fil(rfd);
    }

    printf("mkbtronfs: processing manifest '%s'\n", manifest);
    int ret = process_manifest(manifest);

    /* The budget, read back off the mount rather than counted by the tool that just spent it:
     * this is the row that answers "can another cartridge go in?" without a second opinion. */
    {
        UW bsize = vol_block_size(g_sys_vol);
        UW used  = vol_total_blocks(g_sys_vol) - vol_free_blocks(g_sys_vol);
        printf("mkbtronfs: '%s' holds %u of %u %u-byte blocks: %u KiB used, %u KiB free "
               "(table of %u bodies max)\n",
               output, (unsigned)used, (unsigned)vol_total_blocks(g_sys_vol), (unsigned)bsize,
               (unsigned)(used * bsize / 1024u),
               (unsigned)(vol_free_blocks(g_sys_vol) * bsize / 1024u),
               (unsigned)vol_nfmax(g_sys_vol));
    }

    vol_sync(g_sys_vol);
    vol_umount(g_sys_vol);
    g_sys_vol = NULL;
    blk_file_close(dev);

    if (ret == 0) {
        printf("mkbtronfs: wrote '%s' successfully.\n", output);
        return 0;
    } else {
        fprintf(stderr, "mkbtronfs: some entries failed.\n");
        return 1;
    }
}

