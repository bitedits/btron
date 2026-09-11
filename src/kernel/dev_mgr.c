/*
 * B-TRON Device Management Subsystem: dev_mgr.c
 * Cleanroom implementation of BTRON 3.20 Device Driver Interface.
 */

#include <btron/device.h>
#include <btron/types.h>
#include <btron/error.h>
#include <string.h>
#include <pthread.h>

#define MAX_DEVICES 16

typedef struct {
    ID       dev_id;
    char     name[32];
    UW       mode;
    BOOL     open;
    UB       buffer[1024];
    int      buf_len;
} BTRON_DEV;

static BTRON_DEV g_devs[MAX_DEVICES];
static pthread_mutex_t g_dev_lock = PTHREAD_MUTEX_INITIALIZER;
static ID g_next_dev_id = 1;
static BOOL g_dev_init = FALSE;

static void dev_init_once(void) {
    if (!g_dev_init) {
        memset(g_devs, 0, sizeof(g_devs));
        g_dev_init = TRUE;
    }
}

ID opn_dev_mgr(const char *dev_name, UW mode) {
    if (!dev_name) return ER_PAR;

    pthread_mutex_lock(&g_dev_lock);
    dev_init_once();

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!g_devs[i].open) {
            g_devs[i].dev_id = g_next_dev_id++;
            strncpy(g_devs[i].name, dev_name, sizeof(g_devs[i].name) - 1);
            g_devs[i].mode = mode;
            g_devs[i].open = TRUE;
            g_devs[i].buf_len = 0;

            ID did = g_devs[i].dev_id;
            pthread_mutex_unlock(&g_dev_lock);
            return did;
        }
    }

    pthread_mutex_unlock(&g_dev_lock);
    return ER_NOSPC;
}

ER cls_dev_mgr(ID dev_id) {
    if (dev_id <= 0) return ER_DID;

    pthread_mutex_lock(&g_dev_lock);
    dev_init_once();

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devs[i].open && g_devs[i].dev_id == dev_id) {
            g_devs[i].open = FALSE;
            pthread_mutex_unlock(&g_dev_lock);
            return E_OK;
        }
    }

    pthread_mutex_unlock(&g_dev_lock);
    return ER_DID;
}

ER rea_dev(ID dev_id, VP buf, W sz, W *read_sz) {
    if (!buf || sz < 0) return ER_PAR;
    if (dev_id <= 0) return ER_DID;

    pthread_mutex_lock(&g_dev_lock);
    dev_init_once();

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devs[i].open && g_devs[i].dev_id == dev_id) {
            int to_read = (sz < g_devs[i].buf_len) ? sz : g_devs[i].buf_len;
            if (to_read > 0) {
                memcpy(buf, g_devs[i].buffer, to_read);
                /* Shift remaining bytes */
                memmove(g_devs[i].buffer, g_devs[i].buffer + to_read, g_devs[i].buf_len - to_read);
                g_devs[i].buf_len -= to_read;
            }
            if (read_sz) *read_sz = to_read;
            pthread_mutex_unlock(&g_dev_lock);
            return E_OK;
        }
    }

    pthread_mutex_unlock(&g_dev_lock);
    return ER_DID;
}

ER wri_dev(ID dev_id, const void *buf, W sz, W *wrote_sz) {
    if (!buf || sz < 0) return ER_PAR;
    if (dev_id <= 0) return ER_DID;

    pthread_mutex_lock(&g_dev_lock);
    dev_init_once();

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devs[i].open && g_devs[i].dev_id == dev_id) {
            int space = (int)sizeof(g_devs[i].buffer) - g_devs[i].buf_len;
            int to_write = (sz < space) ? sz : space;
            if (to_write > 0) {
                memcpy(g_devs[i].buffer + g_devs[i].buf_len, buf, to_write);
                g_devs[i].buf_len += to_write;
            }
            if (wrote_sz) *wrote_sz = to_write;
            pthread_mutex_unlock(&g_dev_lock);
            return E_OK;
        }
    }

    pthread_mutex_unlock(&g_dev_lock);
    return ER_DID;
}

ER ctl_dev(ID dev_id, W cmd, VP arg) {
    (void)arg;
    if (dev_id <= 0) return ER_DID;

    pthread_mutex_lock(&g_dev_lock);
    dev_init_once();

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devs[i].open && g_devs[i].dev_id == dev_id) {
            if (cmd == DEV_CMD_FLUSH || cmd == DEV_CMD_RESET) {
                g_devs[i].buf_len = 0;
            }
            pthread_mutex_unlock(&g_dev_lock);
            return E_OK;
        }
    }

    pthread_mutex_unlock(&g_dev_lock);
    return ER_DID;
}

ER ref_dev(ID dev_id, DEV_STAT *stat) {
    if (!stat) return ER_PAR;
    if (dev_id <= 0) return ER_DID;

    pthread_mutex_lock(&g_dev_lock);
    dev_init_once();

    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devs[i].open && g_devs[i].dev_id == dev_id) {
            stat->dev_type = 1;
            stat->mode     = g_devs[i].mode;
            stat->buf_size = sizeof(g_devs[i].buffer);
            stat->status   = 0;
            pthread_mutex_unlock(&g_dev_lock);
            return E_OK;
        }
    }

    pthread_mutex_unlock(&g_dev_lock);
    return ER_DID;
}

ER wai_dev(ID dev_id, DEV_REQ *req, W tmo) {
    (void)tmo;
    if (!req) return ER_PAR;
    if (dev_id <= 0) return ER_DID;
    req->result = E_OK;
    return E_OK;
}
