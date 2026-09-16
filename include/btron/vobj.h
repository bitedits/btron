/*
 * B-TRON Specification Compatible Header: vobj.h
 * Real Object / Virtual Body Hyper-Data Engine Header.
 */

#ifndef _BTRON_VOBJ_H_
#define _BTRON_VOBJ_H_

#include <btron/types.h>
#include <btron/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VOBJ_TYPE_TEXT = 1,
    VOBJ_TYPE_DRAW,
    VOBJ_TYPE_EXEC,
    VOBJ_TYPE_FOLDER,
    VOBJ_TYPE_TERMINAL
} VOBJ_TYPE;

typedef struct {
    ID        robj_id;      /* Real Object Identifier */
    VOBJ_TYPE type;         /* Data Type */
    char      name[64];     /* Real Object Name */
    char      path[256];    /* Disk Storage Path */
    UW        size;         /* Content Payload Size */
} ROBJ;

typedef struct {
    ID        vobj_id;      /* Virtual Body Pointer ID */
    ID        target_robj;  /* Target Real Object ID linked */
    char      label[64];    /* Link display label */
    PNT       pos;          /* Embedded icon position in document/window */
} VOBJ_LINK;

ER init_vobj_sys(const char *storage_root);
ROBJ* cre_robj(const char *name, VOBJ_TYPE type);
ROBJ* opn_robj(ID robj_id);
ER cls_robj(ROBJ *robj);

ROBJ* find_robj_by_path(const char *path);
ROBJ* get_or_create_robj_for_file(const char *path, const char *name, VOBJ_TYPE type);
int   get_robj_count(void);
ROBJ* get_robj_by_index(int idx);

VOBJ_LINK* cre_vobj_link(ID target_robj_id, const char *label, H x, H y);
ER rd_vobj_data(ROBJ *robj, void *buf, UW len, UW *read_bytes);
ER wr_vobj_data(ROBJ *robj, const void *buf, UW len);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_VOBJ_H_ */
