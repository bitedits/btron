/*
 * mkbtronfs — B-System BTRON volume image builder
 *
 * Usage:
 *   mkbtronfs manifest.txt [-o output.vol] [-n NFMAX] [-b NLB]
 *
 * Manifest format (one entry per line, '#' comments allowed):
 *   FILE "name"   TAD   [payload_file_path|-]
 *   FILE "name"   LINK  [target_fid|-]
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

/* ── Insert RT_LINK record into root container (FID 0) ──────────── */
static void add_root_link(const char *name, FID fid)
{
    ID root_fd = opn_fil("SYS", 0x0002 /* F_WRITE */);
    if (root_fd < 0) return;

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

    OpenFile *of = &g_open_files[(int)root_fd];
    int rec_idx = (int)of->nrec;
    ER err = ins_rec(root_fd, rec_idx, payload, (int)(16 + nlen));
    if (err == 0) {
        fil_set_rec_type(root_fd, rec_idx, (UH)RT_LINK);
    }
    cls_fil(root_fd);
}

/* ── Imprint one TAD file ───────────────────────────────────────── */
static int imprint_tad(const char *vol_name_entry, const char *payload_path)
{
    unsigned char *payload = NULL;
    size_t payload_len = 0;

    if (payload_path && strcmp(payload_path, "-") != 0) {
        /* Load from file */
        FILE *pf = fopen(payload_path, "rb");
        if (pf) {
            fseek(pf, 0, SEEK_END);
            long fsz = ftell(pf);
            rewind(pf);
            if (fsz > 0) {
                payload = (unsigned char *)malloc((size_t)fsz);
                if (payload) {
                    payload_len = fread(payload, 1, (size_t)fsz, pf);
                }
            }
            fclose(pf);
        } else {
            fprintf(stderr, "mkbtronfs: warning: cannot open payload '%s': %s\n",
                    payload_path, strerror(errno));
        }
    }

    if (!payload) {
        /* Empty placeholder: just a TS_TEXT with the file name */
        payload = make_tad_text(vol_name_entry, &payload_len);
    }

    /* Create the Real Body */
    ID fd = cre_fil(vol_name_entry, 0x0002 /* F_WRITE */);
    if (fd < 0) {
        fprintf(stderr, "mkbtronfs: cre_fil('%s') failed\n", vol_name_entry);
        free(payload);
        return -1;
    }

    FID fid = g_open_files[(int)fd].fid;

    ER err = ins_rec(fd, 0, payload, (int)payload_len);
    if (err == 0) {
        /* Mark record as RT_TADDATA (type 1) via public helper */
        fil_set_rec_type(fd, 0, (unsigned short)RT_TADDATA);
    }

    cls_fil(fd);
    free(payload);

    if (err == 0) {
        add_root_link(vol_name_entry, fid);
    }
    return (err == 0) ? 0 : -1;
}

/* ── Imprint one LINK file ──────────────────────────────────────── */
static int imprint_link(const char *link_name, int target_fid_num)
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
        add_root_link(link_name, fid);
    }
    return 0;
}

/* ── Parse and execute manifest ─────────────────────────────────── */
static int process_manifest(const char *manifest_path)
{
    FILE *mf = fopen(manifest_path, "r");
    if (!mf) {
        fprintf(stderr, "mkbtronfs: cannot open manifest '%s': %s\n",
                manifest_path, strerror(errno));
        return -1;
    }

    char line[512];
    int errors = 0;
    while (fgets(line, sizeof(line), mf)) {
        /* Strip newline */
        size_t ln = strlen(line);
        while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) {
            line[--ln] = '\0';
        }
        /* Skip comments and blank lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0') continue;

        /* Parse keyword */
        char keyword[16] = {0};
        int ki = 0;
        while (*p && *p != ' ' && *p != '\t' && ki < 15) keyword[ki++] = *p++;
        keyword[ki] = '\0';
        while (*p == ' ' || *p == '\t') p++;

        if (strcmp(keyword, "FILE") != 0) continue;

        /* Parse quoted name */
        if (*p != '"') continue;
        p++;
        char fname[80] = {0};
        int fi = 0;
        while (*p && *p != '"' && fi < 79) fname[fi++] = *p++;
        fname[fi] = '\0';
        if (*p == '"') p++;
        while (*p == ' ' || *p == '\t') p++;

        /* Parse type */
        char ftype[16] = {0};
        int ti = 0;
        while (*p && *p != ' ' && *p != '\t' && ti < 15) ftype[ti++] = *p++;
        ftype[ti] = '\0';
        while (*p == ' ' || *p == '\t') p++;

        /* Parse optional payload / target */
        char extra[256] = {0};
        int ei = 0;
        while (*p && ei < 255) extra[ei++] = *p++;
        extra[ei] = '\0';

        if (strcmp(ftype, "TAD") == 0) {
            printf("  [TAD ] %s\n", fname);
            if (imprint_tad(fname, extra[0] ? extra : NULL) != 0)
                errors++;
        } else if (strcmp(ftype, "LINK") == 0) {
            int target_fid = 0; /* default: root */
            if (extra[0] && extra[0] != '/') target_fid = atoi(extra);
            printf("  [LINK] %s -> FID%d\n", fname, target_fid);
            if (imprint_link(fname, target_fid) != 0)
                errors++;
        } else {
            fprintf(stderr, "mkbtronfs: unknown type '%s' for '%s'\n", ftype, fname);
        }
    }
    fclose(mf);
    return errors ? -1 : 0;
}

/* ── main ────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    const char *manifest = NULL;
    const char *output   = DEFAULT_OUTPUT;
    unsigned int nfmax   = DEFAULT_NFMAX;
    unsigned int nlb     = DEFAULT_NLB;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            nfmax = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            nlb = (unsigned int)atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            manifest = argv[i];
        } else {
            fprintf(stderr, "mkbtronfs: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!manifest) {
        fprintf(stderr, "Usage: mkbtronfs manifest.txt [-o out.vol] [-n NFMAX] [-b NLB]\n");
        return 1;
    }

    printf("mkbtronfs: formatting '%s'  NFMAX=%u NLB=%u (%u KiB)\n",
           output, nfmax, nlb, nlb);

    /* Create and format the volume */
    BlkDev *dev = blk_file_create(output, 1 /* create_new */, (unsigned int)nlb);
    if (!dev) {
        fprintf(stderr, "mkbtronfs: cannot create output file '%s'\n", output);
        return 1;
    }

    if (vol_format(dev, (unsigned int)nfmax, (unsigned int)nlb, DEFAULT_VOLNAME) != 0) {
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

    printf("mkbtronfs: processing manifest '%s'\n", manifest);
    int ret = process_manifest(manifest);

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
