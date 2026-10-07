/*
 * tronMSX — error codes and their strings.  B-MSX Rev 1.05, §4.3.
 * The numeric ABI is stable: values 0…9 are launch codes, 10…12 belong to the
 * debugger, and later cores append above 12 without renumbering.
 */

#ifndef _BTRON_EMULATOR_MSX_ERR_H_
#define _BTRON_EMULATOR_MSX_ERR_H_

#include <emulators/tronmsx.h>   /* the code enumeration lives in the public header */

#define MSX_ERR_COUNT 13

/* The window layer renders the string, never the number (§4.3). */
static inline const char *msx_err_message(int code)
{
    static const char *const s[MSX_ERR_COUNT] = {
        "ok",
        "BIOS file named by the settings key is unreadable",
        "BIOS settings key is absent; C-BIOS is used",
        "BMSX record magic or version is wrong",
        "cartridge header or kind payload law failed",
        "entry vector is zero or outside its page",
        "payload size is zero or over the first-core limit",
        "machine RAM exceeds the first-core pool",
        "kind is not loadable on this core",
        "machine value is unknown",
        "checkpoint belongs to another ROM/BIOS pair",
        "requested frame is outside the input log",
        "no machine instance, or the call was mid-frame"
    };
    if (code < 0 || code >= MSX_ERR_COUNT) return "unknown MSX error";
    return s[code];
}

#endif /* _BTRON_EMULATOR_MSX_ERR_H_ */
