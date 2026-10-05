/*
 * B-System BTRON3 — tty.h
 * Portable termios-scope terminal API for the CLU applications (tv, sc).
 *
 * The subset of POSIX termios that Terminal Vision and Sokhatsky Commander
 * need, with the *same semantics* on every B-System target:
 *
 *   c_lflag : ICANON ECHO IEXTEN ISIG        c_iflag : IXON ICRNL
 *   c_cc    : VMIN VTIME                      TCSANOW / TCSAFLUSH
 *   window size (TIOCGWINSZ) and resize notification (SIGWINCH)
 *
 * A target supplies a raw byte device (BtTtyOps).  Hosted POSIX builds get
 * one automatically backed by the host's termios; qemu/bare-metal kernels
 * register their console (UART / gterm) with bt_tty_set_ops().  Canonical
 * line editing, echo, ICRNL and VMIN/VTIME are implemented in software so the
 * behaviour is identical (and unit-testable) everywhere.
 */
#ifndef BTRON_TTY_H
#define BTRON_TTY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* c_iflag */
#define BT_IXON    0x0001u
#define BT_ICRNL   0x0002u
/* c_lflag */
#define BT_ICANON  0x0001u
#define BT_ECHO    0x0002u
#define BT_IEXTEN  0x0004u
#define BT_ISIG    0x0008u
/* c_cc indices */
#define BT_VMIN    0
#define BT_VTIME   1
#define BT_NCCS    2
/* tcsetattr actions */
#define BT_TCSANOW   0
#define BT_TCSAFLUSH 1

typedef struct {
    uint32_t c_iflag;
    uint32_t c_lflag;
    uint8_t  c_cc[BT_NCCS];
} BtTermios;

/*
 * Raw device contract.  All callbacks must be non-recursive and bounded.
 *   read  : up to n bytes; waits at most timeout_ms (<0: forever, 0: poll).
 *           returns >0 bytes read, 0 on timeout, <0 on error / end of input.
 *   write : returns bytes written (>0), or <0 on error.
 *   winsize: fills rows/cols, returns 0 or <0.
 *   raw   : optional. on=1 puts the device into 8-bit raw mode (keeping the
 *           device's own signal keys if isig!=0), on=0 restores it to its
 *           original state.  A device that provides raw() owns canonical
 *           processing: ICANON is then delivered by the device in raw(0).
 */
typedef struct {
    long (*read)(void *buf, size_t n, int timeout_ms);
    long (*write)(const void *buf, size_t n);
    int  (*winsize)(int *rows, int *cols);
    int  (*raw)(int on, int isig);
} BtTtyOps;

void bt_tty_set_ops(const BtTtyOps *ops);   /* NULL restores the default */
int  bt_tty_available(void);                /* 1 if a device is attached */

int  bt_tcgetattr(BtTermios *t);
int  bt_tcsetattr(int action, const BtTermios *t);
int  bt_tcgetwinsize(int *rows, int *cols);

/* VMIN/VTIME-aware read (see termios(3)).  Returns bytes, 0 timeout, <0 err. */
long bt_tty_read(void *buf, size_t n);
/* Wait up to timeout_ms for input; 1 if a byte is ready, 0 timeout, <0 err. */
int  bt_tty_poll(int timeout_ms);
/* Write all n bytes; returns n or <0. */
long bt_tty_write(const void *buf, size_t n);

/* Bytes written to the device if the process is killed while in raw mode. */
void bt_tty_set_exit_seq(const char *seq);

/* Resize notification: set from SIGWINCH / the kernel; consumed by apps. */
void bt_tty_notify_resize(void);
int  bt_tty_take_resize(void);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_TTY_H */
