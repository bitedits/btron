#ifndef LILCU64_INSERT_H
#define LILCU64_INSERT_H

#include <stdint.h>
#include <stddef.h>

#define LC_INS_MAX 128
#define LC_LINE_MAX 8

void lc_insert_reset(void);
void lc_insert_line_zero(int kind, int line);
void lc_insert_state_save(void);
void lc_insert_state_restore(void);
void lc_curve_build(float *dst, int k);
float lc_curve_read(const float *curve, float x);

/* id is OrchInsertId. line is 0..7 for delay and chorus, else ignored.
 * Returns 0 when the line was clamped. */
int lc_insert_apply(int inst, int id, const int16_t *p, float *x, int n,
                    uint32_t fs, uint32_t bpm, uint32_t den,
                    const float *curve, uint64_t pos, int line);

#endif
