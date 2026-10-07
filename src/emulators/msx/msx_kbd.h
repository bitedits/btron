/*
 * tronMSX — keyboard matrix.  B-MSX Rev 1.05, §2.5.
 *
 * The MSX1 matrix is 16 strobe rows by 8 column bits: the row is a binary
 * value written to PPI port C (C-BIOS SNSMAT does `IN (0xAA) / OUT (0xAA)` then
 * `IN (0xA9)`), and the columns are read on PPI port B, active low.  This is
 * the matrix C-BIOS and MSX1 cartridges actually address; the 8x10 PSG-strobe
 * description is the MSX2 path and maps onto rows 0..7 of the same array.
 */

#ifndef _BTRON_EMULATOR_MSX_KBD_H_
#define _BTRON_EMULATOR_MSX_KBD_H_

#include <stdint.h>

#define MSX_KBD_ROWS 16u
#define MSX_KBD_COLS 8u

/* Named non-printable positions: {row, column bit}. */
#define MSX_KEY_SPACE_R   8u
#define MSX_KEY_SPACE_C   0u
#define MSX_KEY_HOME_R    8u
#define MSX_KEY_HOME_C    1u
#define MSX_KEY_INS_R     8u
#define MSX_KEY_INS_C     2u
#define MSX_KEY_DEL_R     8u
#define MSX_KEY_DEL_C     3u
#define MSX_KEY_LEFT_R    8u
#define MSX_KEY_LEFT_C    4u
#define MSX_KEY_UP_R      8u
#define MSX_KEY_UP_C      5u
#define MSX_KEY_DOWN_R    8u
#define MSX_KEY_DOWN_C    6u
#define MSX_KEY_RIGHT_R   8u
#define MSX_KEY_RIGHT_C   7u

#define MSX_KEY_TAB_R     7u
#define MSX_KEY_TAB_C     3u
#define MSX_KEY_STOP_R    7u
#define MSX_KEY_STOP_C    4u
#define MSX_KEY_BS_R      7u
#define MSX_KEY_BS_C      5u
#define MSX_KEY_BOS_R     7u   /* SELECT / BOS */
#define MSX_KEY_BOS_C     6u
#define MSX_KEY_ENTER_R   7u
#define MSX_KEY_ENTER_C   7u

#define MSX_KEY_F4_R      7u
#define MSX_KEY_F4_C      0u
#define MSX_KEY_F5_R      7u
#define MSX_KEY_F5_C      1u
#define MSX_KEY_ESC_R     7u
#define MSX_KEY_ESC_C     2u

#define MSX_KEY_SHIFT_R   6u
#define MSX_KEY_SHIFT_C   0u
#define MSX_KEY_CTRL_R    6u
#define MSX_KEY_CTRL_C    1u
#define MSX_KEY_GRAPH_R   6u
#define MSX_KEY_GRAPH_C   2u   /* CAPS-LOCK position on 50-key layouts */
#define MSX_KEY_CAPS_R    6u
#define MSX_KEY_CAPS_C    3u
#define MSX_KEY_CODE_R    6u   /* KANA / CODE */
#define MSX_KEY_CODE_C    4u
#define MSX_KEY_F1_R      6u
#define MSX_KEY_F1_C      5u
#define MSX_KEY_F2_R      6u
#define MSX_KEY_F2_C      6u
#define MSX_KEY_F3_R      6u
#define MSX_KEY_F3_C      7u

#define MSX_KBD_NO_KEY    0xFFu

/* One generated array: ASCII (0x20…0x7F) to {row, column}, 0xFF where the
 * character has no key of its own.  Lower-case letters need SHIFT, which the
 * caller presses separately; the position is the same either way. */
static const uint8_t msx_kbd_ascii_row[96] = {
    /*  ' '  !    "    #    $    %    &    '  */
    8, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 2,
    /*  (    )    *    +    ,    -    .    /  */
    0xFF,0xFF, 9, 9, 2, 1, 2, 2,
    /*  0    1    2    3    4    5    6    7   */
    0, 0, 0, 0, 0, 0, 0, 0,
    /*  8    9    :    ;    <    =    >    ?   */
    1, 1, 0xFF, 1, 0xFF, 1, 0xFF, 0xFF,
    /*  @    A    B    C    D    E    F    G   */
    0xFF, 2, 2, 3, 3, 3, 3, 3,
    /*  H    I    J    K    L    M    N    O   */
    3, 3, 3, 4, 4, 4, 4, 4,
    /*  P    Q    R    S    T    U    V    W   */
    4, 4, 4, 5, 5, 5, 5, 5,
    /*  X    Y    Z    [    \    ]    ^    _   */
    5, 5, 5, 1, 1, 1, 0xFF,0xFF,
    /*  `    a    b    c    d    e    f    g   */
    2, 2, 2, 3, 3, 3, 3, 3,
    /*  h    i    j    k    l    m    n    o   */
    3, 3, 3, 4, 4, 4, 4, 4,
    /*  p    q    r    s    t    u    v    w   */
    4, 4, 4, 5, 5, 5, 5, 5,
    /*  x    y    z    {    |    }    ~    DEL */
    5, 5, 5, 0xFF,0xFF,0xFF,0xFF,0xFF
};

static const uint8_t msx_kbd_ascii_col[96] = {
    /*  ' '  !    "    #    $    %    &    '  */
    0, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0,
    /*  (    )    *    +    ,    -    .    /  */
    0xFF,0xFF, 1, 3, 2, 2, 3, 4,
    /*  0    1    2    3    4    5    6    7   */
    0, 1, 2, 3, 4, 5, 6, 7,
    /*  8    9    :    ;    <    =    >    ?   */
    0, 1, 0xFF, 7, 0xFF, 3, 0xFF, 0xFF,
    /*  @    A    B    C    D    E    F    G   */
    0xFF, 6, 7, 0, 1, 2, 3, 4,
    /*  H    I    J    K    L    M    N    O   */
    5, 6, 7, 0, 1, 2, 3, 4,
    /*  P    Q    R    S    T    U    V    W   */
    5, 6, 7, 0, 1, 2, 3, 4,
    /*  X    Y    Z    [    \    ]    ^    _   */
    5, 6, 7, 5, 4, 6, 0xFF,0xFF,
    /*  `    a    b    c    d    e    f    g   */
    1, 6, 7, 0, 1, 2, 3, 4,
    /*  h    i    j    k    l    m    n    o   */
    5, 6, 7, 0, 1, 2, 3, 4,
    /*  p    q    r    s    t    u    v    w   */
    5, 6, 7, 0, 1, 2, 3, 4,
    /*  x    y    z    {    |    }    ~    DEL */
    5, 6, 7, 0xFF,0xFF,0xFF,0xFF,0xFF
};

/* -> 1 when the character has a key; row/col are written only then. */
static inline int msx_kbd_position(char ch, uint8_t *row, uint8_t *col)
{
    uint8_t idx;
    if ((uint8_t)ch < 0x20u || (uint8_t)ch >= 0x80u) return 0;
    idx = (uint8_t)(ch - 0x20);
    if (msx_kbd_ascii_row[idx] == MSX_KBD_NO_KEY) return 0;
    *row = msx_kbd_ascii_row[idx];
    *col = msx_kbd_ascii_col[idx];
    return 1;
}

#endif /* _BTRON_EMULATOR_MSX_KBD_H_ */
