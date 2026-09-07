/*
 * BTRON 3.20 Font Manager (Менеджер шрифтів) API
 * include/btron/font_mgr.h
 *
 * Normative source: b-spec/os_spec/shell/font_mgr.html
 */

#ifndef __BTRON_FONT_MGR_H__
#define __BTRON_FONT_MGR_H__

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Placement / Spec Types ───────────────────────────────────── */
#define FT_MEM          0x0001  /* Fixed in ROM / system memory */
#define FT_FILE         0x0002  /* Dynamic font loaded from real object file */
#define FT_RES          0x0004  /* Resource font */
#define FT_SYSTEM       0x0008  /* System font */

/* ── Font Classes ─────────────────────────────────────────────── */
#define FTC_DEFAULT     0
#define FTC_MINCHO      1
#define FTC_GOTHIC      2
#define FTC_MARUGO      3
#define FTC_SERIF       4
#define FTC_SANSSERIF   5
#define FTC_SYMBOL      6

/* ── List Target Filters ──────────────────────────────────────── */
#define FT_ALL          0x0000
#define FT_FAMILY       0x0001
#define FT_SCALL        0x0002

/* ── Glyph Image Retrieval Modes ──────────────────────────────── */
#define FT_IMAGE        0x0001  /* Return raster bitmap */
#define FT_SYS          0x0002  /* Allocate in system memory */

/* ── Error Codes Specific to Font Manager ─────────────────────── */
#define EX_FTID         (-201)  /* Font ID does not exist */
#define EX_FTD          (-202)  /* Font descriptor does not exist */
#define EX_NOSPC        (-10)   /* Out of memory / buffer space */
#define EX_PAR          (-33)   /* Parameter error */
#define EX_ADR          (-34)   /* Invalid address pointer */
#define EX_NOMEM        (-10)   /* Out of memory */

/* ── Data Types ───────────────────────────────────────────────── */
typedef VP      FLOC;           /* Font location pointer / link */
typedef H       FCLASS;         /* Font class */
typedef UW      FATTR;          /* Font style attributes */
typedef W       OFFSET;         /* Byte offset */

typedef struct {
    H width;
    H height;
} SIZE;

/* □ Font Definition Header (FDEF) */
typedef struct {
    H           fclass;         /* Font class (FTC_MINCHO, FTC_DEFAULT, etc.) */
    H           attr;           /* Attributes (slant, bold, etc.) */
    TC          name[12];       /* Family name */
    UB          size;           /* Base raster height */
    UB          width;          /* Max character width */
    UB          base;           /* Baseline */
    UB          leading;        /* Leading */
    TC          fullname[20];   /* Full name */
    TC          topcode;        /* First character code */
    TC          lastcode;       /* Last character code */
    UB          imgform;        /* 0=fixed, 1=variable, 2=vector, 3=TrueType */
    UB          widform;        /* Width format: 0=simple, 1=indexed, 2=image */
    W           datasize;       /* Size in bytes */
    OFFSET      offimage;       /* Offset to glyph bitmaps */
    OFFSET      offwidth;       /* Offset to width table */
    OFFSET      offnote;        /* Offset to annotation text */
} FDEF;

/* □ Font Set Specification (FSSPEC) */
typedef struct {
    TC          name[12];       /* Family name (NULL = default) */
    FCLASS      fclass;         /* Font class */
    FATTR       attr;           /* Style attributes */
    SIZE        size;           /* Character size (width, height) */
    H           angle;          /* Rotation angle (0..359 degrees) */
} FSSPEC;

/* □ Glyph Character Image Info (FCDATA) */
typedef struct {
    RECT        frame;          /* Character bounding box */
    H           width;          /* Character width */
    H           height;         /* Character height */
    PNT         imgofs;         /* Image offset */
    UH          family;         /* Family ID */
    UH          fid;            /* Font ID */
} FCDATA;

/* □ Glyph Raster Data (FDATA) */
typedef struct {
    UW          attr;           /* Attributes */
    UH          height;         /* Maximum height */
    UH          width;          /* Maximum width */
    UH          base;           /* Baseline */
    UH          leading;        /* Leading */
    SIZE        asize;          /* Character size */
    UH          aangle;         /* Rotation angle */
    UH          pixbits;        /* Bit depth (1, 2, 4, 8) */
    UB          *image;         /* Raster bitmap address */
    FCDATA      ch[1];          /* Array of character metrics */
} FDATA;

/* □ Font List Entry (FLIST) */
typedef struct {
    W           fid;            /* Font ID */
    TC          name[12];       /* Family name */
    FCLASS      fclass;         /* Font class */
    H           size;           /* Nominal size */
    UW          attr;           /* Attributes */
} FLIST;

/* □ Font Family Info (FNTINFO) */
typedef struct {
    TC          name[12];       /* Family name */
    H           base;           /* Baseline */
    H           leading;        /* Leading */
    H           max_width;      /* Maximum width */
    H           max_height;     /* Maximum height */
    UW          attr;           /* Supported attributes */
} FNTINFO;

/* ── System Calls ─────────────────────────────────────────────── */

/* Define / register font in system pool */
WERR fdef_fnt(FLOC loc, W spec);

/* Delete font by ID */
ERR  fdel_fnt(W fid);

/* Delete font by location */
ERR  fdel_loc(FLOC loc, W spec);

/* Get font definition header */
ERR  fget_def(W fid, FDEF *fdef);

/* Get font note / annotation */
WERR fget_not(W fid, TC *note, W size);

/* List available fonts */
WERR flst_fon(W target, FLIST *list, W size);

/* Open font set (descriptor) */
WERR fopn_fon(void);

/* Close font set */
ERR  fcls_fon(W fdesc);

/* Set font set parameters */
ERR  fset_fon(W fdesc, FSSPEC *spec);

/* Get font set parameters */
ERR  fget_fon(W fdesc, FSSPEC *spec);

/* Set font rotation angle */
ERR  fset_ang(W fdesc, W ang);

/* Get font rotation angle */
WERR fget_ang(W fdesc);

/* Get font family information */
WERR fget_fam(W fdesc, W script, FNTINFO *inf);

/* Get glyph raster image and metrics */
WERR fget_img(W fdesc, FDATA *cimg, W size, W script, TC ch, UW mode);

#ifdef __cplusplus
}
#endif

#endif /* __BTRON_FONT_MGR_H__ */
