/*
 * B-System (BTRON 3.20) Cleanroom Unified Image Decoder
 * Decoding PNG & GIF into RGBA pixel buffers.
 */

#ifndef _BTRON_IMAGE_DECODE_H_
#define _BTRON_IMAGE_DECODE_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decode PNG or GIF file into a newly allocated RGBA buffer (4 bytes per pixel).
 * The caller is responsible for freeing *out_pixels with free().
 * Returns 0 on success, negative on failure.
 */
int decode_image_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h);
int decode_png_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h);
int decode_gif_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_IMAGE_DECODE_H_ */
