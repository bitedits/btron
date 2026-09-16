/*
 * B-System (BTRON 3.20) Direct Manipulation Drag-and-Drop Protocol
 */

#ifndef _BTRON_DND_H_
#define _BTRON_DND_H_

#include <btron/types.h>
#include <btron/vobj.h>
#include <btron/dp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BOOL      active;
    ID        source_wndid;
    ID        robj_id;
    VOBJ_TYPE type;
    char      name[64];
    char      path[256];
    H         drag_x;
    H         drag_y;
    H         start_x;
    H         start_y;
} BTRON_DND;

void             btron_dnd_init(void);
void             btron_dnd_begin(ID source_wndid, ID robj_id, VOBJ_TYPE type, const char *name, const char *path, H start_x, H start_y);
void             btron_dnd_update(H x, H y);
void             btron_dnd_end(void);
BOOL             btron_dnd_is_active(void);
const BTRON_DND* btron_dnd_get(void);
void             btron_dnd_render_ghost(GDEV *dev);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_DND_H_ */
