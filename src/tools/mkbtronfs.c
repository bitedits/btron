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

static char g_vol_name[40] = DEFAULT_VOLNAME;

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

/* ── Imprint one TAD file ───────────────────────────────────────── */
static int imprint_tad(const char *vol_name_entry, const char *payload_path, const char *parent_container)
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
        add_container_link(parent_container, vol_name_entry, fid);
    }
    return (err == 0) ? 0 : -1;
}

/* ── Imprint one MD / TXT file ──────────────────────────────────── */
static int imprint_text(const char *vol_name_entry, const char *payload_path, const char *parent_container)
{
    unsigned char *payload = NULL;
    size_t payload_len = 0;

    if (!payload_path || !payload_path[0] || strcmp(payload_path, "-") == 0) {
        fprintf(stderr, "mkbtronfs: error: text entry '%s' requires payload file\n", vol_name_entry);
        return -1;
    }

    FILE *pf = fopen(payload_path, "rb");
    if (!pf) {
        fprintf(stderr, "mkbtronfs: cannot open text payload '%s': %s\n",
                payload_path, strerror(errno));
        return -1;
    }
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

    if (!payload && fsz > 0) {
        fprintf(stderr, "mkbtronfs: malloc failed for '%s'\n", payload_path);
        return -1;
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
        fil_set_rec_type(fd, 0, (unsigned short)RT_TADDATA);
    }

    cls_fil(fd);
    free(payload);

    if (err == 0) {
        add_container_link(parent_container, vol_name_entry, fid);
    }
    return (err == 0) ? 0 : -1;
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

        if (strcmp(keyword, "DIR") == 0) {
            if (*p != '"') continue;
            p++;
            char dname[80] = {0};
            int di = 0;
            while (*p && *p != '"' && di < 79) dname[di++] = *p++;
            dname[di] = '\0';
            if (*p == '"') p++;
            while (*p == ' ' || *p == '\t') p++;
            char parent[80] = {0};
            int pi = 0;
            while (*p && *p != ' ' && *p != '\t' && pi < 79) parent[pi++] = *p++;
            parent[pi] = '\0';
            printf("  [DIR ] %s (in %s)\n", dname, parent[0] && strcmp(parent, "-") != 0 ? parent : g_vol_name);
            if (imprint_dir(dname, parent[0] ? parent : NULL) != 0)
                errors++;
            continue;
        }

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

        /* Parse payload and optional parent container */
        char payload_path[256] = {0};
        char parent_container[80] = {0};
        int pli = 0;
        while (*p && *p != ' ' && *p != '\t' && pli < 255) payload_path[pli++] = *p++;
        payload_path[pli] = '\0';
        while (*p == ' ' || *p == '\t') p++;
        int pci = 0;
        while (*p && *p != ' ' && *p != '\t' && pci < 79) parent_container[pci++] = *p++;
        parent_container[pci] = '\0';

        const char *parent = parent_container[0] ? parent_container : NULL;

        if (strcmp(ftype, "TAD") == 0) {
            printf("  [TAD ] %s\n", fname);
            if (imprint_tad(fname, payload_path[0] ? payload_path : NULL, parent) != 0)
                errors++;
        } else if (strcmp(ftype, "MD") == 0 || strcmp(ftype, "TXT") == 0) {
            printf("  [%-4s] %s (%s) [in %s]\n", ftype, fname, payload_path, parent ? parent : g_vol_name);
            if (imprint_text(fname, payload_path[0] ? payload_path : NULL, parent) != 0)
                errors++;
        } else if (strcmp(ftype, "LINK") == 0) {
            int target_fid = 0; /* default: root */
            if (payload_path[0] && payload_path[0] != '/') target_fid = atoi(payload_path);
            printf("  [LINK] %s -> FID%d\n", fname, target_fid);
            if (imprint_link(fname, target_fid, parent) != 0)
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
    const char *label    = NULL;
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
        } else if (argv[i][0] != '-') {
            manifest = argv[i];
        } else {
            fprintf(stderr, "mkbtronfs: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (!manifest) {
        fprintf(stderr, "Usage: mkbtronfs manifest.txt [-o out.vol] [-n NFMAX] [-b NLB] [-l VOLNAME]\n");
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

