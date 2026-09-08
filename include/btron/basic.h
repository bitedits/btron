/*
 * B-System (BTRON 3.20) – basic.h
 *
 * Fundamental system header.
 * Equivalent of Cho-Kanji / B-right/V <basic.h>
 *
 * Every translation unit should include this first.
 */

#ifndef _BTRON_BASIC_H_
#define _BTRON_BASIC_H_

/* ---------------------------------------------------------------
 * 1. Basic data types  (typedef.h equivalent)
 * --------------------------------------------------------------- */
#include <btron/types.h>          /* B, H, W, UB, UH, UW, ID, ER, ... */

/* ---------------------------------------------------------------
 * 2. Platform / specification information  (machine.h equivalent)
 * --------------------------------------------------------------- */

/* TRON family specification identifiers */
#define SPEC_TRON     0x0000      /* TRON common (TAD etc.)    */
#define SPEC_ITRON    0x1000      /* ITRON                     */
#define SPEC_BTRON    0x2000      /* BTRON B-System            */
#define SPEC_CTRON    0x3000      /* CTRON                     */
#define SPEC_mITRON   0x5000      /* μITRON T-Kernel 2.0       */
#define SPEC_mBTRON   0x6000      /* μBTRON FOMA               */

/* B-System identification */
#define BTRON_SPEC    (SPEC_BTRON | 0x0320)   /* BTRON 3.20 */

#ifndef BSYSTEM_VERSION
#define BSYSTEM_VERSION  0x0320               /* 3.20 */
#endif

/* ---------------------------------------------------------------
 * 3. Core subsystem headers that almost every program needs
 * --------------------------------------------------------------- */
#include <btron/error.h>          /* ER, error codes               */
#include <btron/tad.h>            /* Record types, TAD segments    */

/* Optional but commonly used – uncomment as needed
#include <btron/vobj.h>           // Real Body / Virtual Body API
#include <btron/troncode.h>       // TRON character code helpers
*/

#ifdef __cplusplus
extern "C" {
#endif

/* Any global convenience macros or inline helpers can go here */

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_BASIC_H_ */
