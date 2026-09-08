/*
 * B-TRON / B-System Specification Compatible Header: tad.h
 *
 * TRON Application Databus (TAD) + Real Body record types
 * and related constants.
 *
 * Based on BTRON3 / B-right/V public documentation.
 */

#ifndef _BTRON_TAD_H_
#define _BTRON_TAD_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Record Types (Real Body records)
 * ================================================================ */

#define RT_LINK       0   /* Link record (Virtual Body)          */
#define RT_TADDATA    1   /* TAD main record                     */
#define RT_TADCMT     2   /* TAD comment / annotation record     */
#define RT_TADSUB     3   /* TAD auxiliary record                */
#define RT_TADRSV     4   /* Reserved                            */
#define RT_SFUSEN     5   /* Setting fusen (sticker)             */
#define RT_DFUSEN     6   /* Designated fusen                    */
#define RT_FFUSEN     7   /* Function fusen                      */
#define RT_MFUSEN     8   /* Execution-function fusen            */
#define RT_PROG       9   /* Executable program record           */
#define RT_DATABOX   10   /* Data box record                     */
#define RT_FONT      11   /* Font data record                    */
#define RT_DICT      12   /* Dictionary data record              */
#define RT_SYSRSV1   13   /* System reserved 1                   */
#define RT_SYSRSV2   14   /* System reserved 2                   */
#define RT_SYSDATA   15   /* System data record                  */
/* 16–31 : Application-defined records                           */

/* Record type masks (useful for bit flags) */
#define RM_LINK      (1u <<  0)
#define RM_TADDATA   (1u <<  1)
#define RM_TADCMT    (1u <<  2)
#define RM_TADSUB    (1u <<  3)
#define RM_TADRSV    (1u <<  4)
#define RM_SFUSEN    (1u <<  5)
#define RM_DFUSEN    (1u <<  6)
#define RM_FFUSEN    (1u <<  7)
#define RM_MFUSEN    (1u <<  8)
#define RM_PROG      (1u <<  9)
#define RM_DATABOX   (1u << 10)
#define RM_FONT      (1u << 11)
#define RM_DICT      (1u << 12)
#define RM_SYSRSV1   (1u << 13)
#define RM_SYSRSV2   (1u << 14)
#define RM_SYSDATA   (1u << 15)

/* ================================================================
 * Real Body / File type flags (File Header)
 * ================================================================ */

#define OBJ_EJECT    0x1000   /* Removable device Real Body       */
#define OBJ_DEV      0x2000   /* Device Real Body                 */
#define OBJ_EXEC     0x8000   /* Executable Real Body             */

/* ================================================================
 * Standard variable-length TAD Segment IDs
 * (after the 0xFF escape byte)
 * ================================================================ */

#define TS_INFO      0xE0     /* Management / Info segment        */
#define TS_TEXT      0xE1     /* Text start                       */
#define TS_TEXTEND   0xE2     /* Text end                         */
#define TS_FIG       0xE3     /* Figure / Drawing start            */
#define TS_FIGEND    0xE4     /* Figure end                        */
#define TS_IMAGE     0xE5     /* Image segment                    */
#define TS_VOBJ      0xE6     /* Virtual Body (link) segment      */
#define TS_DFUSEN    0xE7     /* Designated fusen segment         */
#define TS_FFUSEN    0xE8     /* Function fusen segment           */
#define TS_SFUSEN    0xE9     /* Setting fusen segment            */

/* ================================================================
 * Frequently used TRON character codes
 * ================================================================ */

#define TC_NULL      0x0000
#define TC_NL        0x000a   /* New paragraph                    */
#define TC_CR        0x000d   /* Carriage return / new line       */
#define TC_TAB       0x0009
#define TC_FF        0x000c   /* Form feed                        */
#define TC_NC        0x000b   /* New column                       */
#define TC_SP        0x0020   /* Space                            */
#define TC_LANG      0xfe00   /* Language specifier               */
#define TC_SPEC      0xff00   /* Special code range               */
#define TC_ESC       0xff80   /* Escape                           */

/* ================================================================
 * Basic TAD segment header (variable-length segments)
 *
 * Layout (big-endian for classic volumes, little-endian for
 * "quasi-TAD" used on many x86 implementations):
 *
 *   +0  UB  0xFF          escape
 *   +1  UB  segment_id    (TS_*)
 *   +2  UH  length        (data size that follows)
 *   +4  ... data ...
 * ================================================================ */

typedef struct {
    UB   esc;          /* always 0xFF for variable-length */
    UB   id;           /* TS_* segment identifier         */
    UH   len;          /* length of the following data    */
    /* UB data[len]; */
} TAD_SEG_HDR;

/* ================================================================
 * Minimal Record Index descriptor (16 bytes on disk)
 * (conceptual; exact bit packing depends on index level)
 * ================================================================ */

typedef struct {
    UH   kind;         /* distinguishes normal / link / continuation */
    UH   type;         /* record type (RT_*) + subtype bits          */
    UW   size;         /* payload size in bytes                      */
    UW   offset;       /* byte offset into Data Blocks               */
    /* remaining bytes of the 16-byte entry are flags / reserved     */
} RECORD_INDEX;

/* ================================================================
 * Convenience helpers (optional, implementation-defined)
 * ================================================================ */

/* Returns true if the record type is a TAD-carrying record */
static inline int tad_is_tad_record(UH type)
{
    return (type == RT_TADDATA ||
            type == RT_TADCMT  ||
            type == RT_TADSUB  ||
            type == RT_SFUSEN  ||
            type == RT_DFUSEN  ||
            type == RT_FFUSEN  ||
            type == RT_MFUSEN);
}

/* Returns true if the record type is an executable program */
static inline int tad_is_program(UH type)
{
    return (type == RT_PROG);
}

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_TAD_H_ */
