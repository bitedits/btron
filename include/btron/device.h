/*
 * B-TRON Specification Compatible Header: device.h
 * Device Management Subsystem (BTRON 3.20 Driver Interface).
 */

#ifndef _BTRON_DEVICE_H_
#define _BTRON_DEVICE_H_

#include <btron/types.h>
#include <btron/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Device Open Modes ─────────────────────────────────────────── */
#define DEV_READ     0x0001U
#define DEV_WRITE    0x0002U
#define DEV_EXCL     0x0004U

/* ── Device Control Commands ───────────────────────────────────── */
#define DEV_CMD_RESET    1
#define DEV_CMD_FLUSH    2
#define DEV_CMD_GET_INFO 3
#define DEV_CMD_SET_BAUD 4

/* ── Device Status Descriptor ──────────────────────────────────── */
typedef struct {
    UW  dev_type;    /* Character, Block, Network, Screen */
    UW  mode;        /* Active open flags */
    UW  buf_size;    /* Internal buffer size */
    UW  status;      /* Ready / Busy / Error flags */
} DEV_STAT;

/* ── Asynchronous Device Request Block ─────────────────────────── */
typedef struct {
    ID   dev_id;
    W    cmd;
    VP   buf;
    W    size;
    ER   result;
} DEV_REQ;

/* ── Device Management APIs ────────────────────────────────────── */
ID opn_dev_mgr(const char *dev_name, UW mode);
ER cls_dev_mgr(ID dev_id);
ER rea_dev(ID dev_id, VP buf, W sz, W *read_sz);
ER wri_dev(ID dev_id, const void *buf, W sz, W *wrote_sz);
ER ctl_dev(ID dev_id, W cmd, VP arg);
ER ref_dev(ID dev_id, DEV_STAT *stat);
ER wai_dev(ID dev_id, DEV_REQ *req, W tmo);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_DEVICE_H_ */
