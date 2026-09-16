/*
 * B-System (BTRON 3.20) Cleanroom Unified Image Decoder: image_decode.c
 * Supports decoding PNG and GIF files to 32-bit RGBA pixel buffers.
 */

#include <btron/image_decode.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MAX_GIF_PIXELS (2048 * 2048)
#define MAX_PNG_IDAT   (8 * 1024 * 1024)
#define MAX_PNG_RAW    (16 * 1024 * 1024)

/* ── Paeth Predictor for PNG Filtering ──────────────────────────────── */
static inline UB paeth_predictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) return (UB)a;
    if (pb <= pc) return (UB)b;
    return (UB)c;
}

/* ── Native PNG Decoder ─────────────────────────────────────────────── */
int decode_png_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h) {
    if (!filepath || !out_pixels || !out_w || !out_h) return -1;
    *out_pixels = NULL;
    *out_w = 0;
    *out_h = 0;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    UB sig[8];
    if (fread(sig, 1, 8, fp) != 8 || sig[0] != 0x89 || sig[1] != 'P' || sig[2] != 'N' || sig[3] != 'G') {
        fclose(fp);
        return -1;
    }

    int img_w = 0, img_h = 0;
    int color_type = 0;
    int idat_len = 0;

    UB *idat_buf = (UB *)malloc(MAX_PNG_IDAT);
    if (!idat_buf) {
        fclose(fp);
        return -1;
    }

    while (!feof(fp)) {
        UB chdr[8];
        if (fread(chdr, 1, 8, fp) != 8) break;
        UW clen = (chdr[0] << 24) | (chdr[1] << 16) | (chdr[2] << 8) | chdr[3];
        char ctype[5] = { chdr[4], chdr[5], chdr[6], chdr[7], 0 };

        if (strcmp(ctype, "IHDR") == 0) {
            UB ihdr[13];
            if (fread(ihdr, 1, 13, fp) != 13) break;
            img_w = (ihdr[0] << 24) | (ihdr[1] << 16) | (ihdr[2] << 8) | ihdr[3];
            img_h = (ihdr[4] << 24) | (ihdr[5] << 16) | (ihdr[6] << 8) | ihdr[7];
            color_type = ihdr[9];
            fseek(fp, 4, SEEK_CUR);
        } else if (strcmp(ctype, "IDAT") == 0) {
            if (idat_len + clen <= MAX_PNG_IDAT) {
                if (fread(idat_buf + idat_len, 1, clen, fp) == clen) {
                    idat_len += clen;
                }
            } else {
                fseek(fp, clen, SEEK_CUR);
            }
            fseek(fp, 4, SEEK_CUR);
        } else if (strcmp(ctype, "IEND") == 0) {
            break;
        } else {
            fseek(fp, clen + 4, SEEK_CUR);
        }
    }
    fclose(fp);

    if (img_w <= 0 || img_h <= 0 || idat_len <= 0) {
        free(idat_buf);
        return -1;
    }

    int bpp = 3;
    if (color_type == 6) bpp = 4;
    else if (color_type == 0) bpp = 1;
    else if (color_type == 2) bpp = 3;

    int row_bytes = img_w * bpp;
    int stride = row_bytes + 1;
    uLongf dest_len = MAX_PNG_RAW;

    UB *raw_buf = (UB *)malloc(MAX_PNG_RAW);
    if (!raw_buf) {
        free(idat_buf);
        return -1;
    }

    if (uncompress(raw_buf, &dest_len, idat_buf, idat_len) != Z_OK) {
        free(raw_buf);
        free(idat_buf);
        return -1;
    }
    free(idat_buf);

    UB *scan_buf = (UB *)malloc((size_t)img_h * row_bytes);
    if (!scan_buf) {
        free(raw_buf);
        return -1;
    }

    for (int y = 0; y < img_h; y++) {
        UB filter = raw_buf[y * stride];
        UB *raw = &raw_buf[y * stride + 1];
        UB *dst = &scan_buf[y * row_bytes];
        UB *prev = (y > 0) ? &scan_buf[(y - 1) * row_bytes] : NULL;

        switch (filter) {
            case 0:
                memcpy(dst, raw, row_bytes);
                break;
            case 1:
                for (int x = 0; x < row_bytes; x++) {
                    UB a = (x >= bpp) ? dst[x - bpp] : 0;
                    dst[x] = raw[x] + a;
                }
                break;
            case 2:
                for (int x = 0; x < row_bytes; x++) {
                    UB b = prev ? prev[x] : 0;
                    dst[x] = raw[x] + b;
                }
                break;
            case 3:
                for (int x = 0; x < row_bytes; x++) {
                    UB a = (x >= bpp) ? dst[x - bpp] : 0;
                    UB b = prev ? prev[x] : 0;
                    dst[x] = raw[x] + ((a + b) >> 1);
                }
                break;
            case 4:
                for (int x = 0; x < row_bytes; x++) {
                    UB a = (x >= bpp) ? dst[x - bpp] : 0;
                    UB b = prev ? prev[x] : 0;
                    UB c = (prev && x >= bpp) ? prev[x - bpp] : 0;
                    dst[x] = raw[x] + paeth_predictor(a, b, c);
                }
                break;
            default:
                memcpy(dst, raw, row_bytes);
                break;
        }
    }
    free(raw_buf);

    /* Allocate output RGBA buffer (4 bytes per pixel) */
    UB *out = (UB *)malloc((size_t)img_w * img_h * 4);
    if (!out) {
        free(scan_buf);
        return -1;
    }

    for (int y = 0; y < img_h; y++) {
        for (int x = 0; x < img_w; x++) {
            UB *src = scan_buf + (y * row_bytes) + (x * bpp);
            UB *dst = out + (y * img_w + x) * 4;

            if (bpp == 4) {
                dst[0] = src[0]; /* R */
                dst[1] = src[1]; /* G */
                dst[2] = src[2]; /* B */
                dst[3] = src[3]; /* A */
            } else if (bpp == 3) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
            } else if (bpp == 1) {
                dst[0] = src[0];
                dst[1] = src[0];
                dst[2] = src[0];
                dst[3] = 255;
            }
        }
    }

    free(scan_buf);
    *out_pixels = out;
    *out_w = (H)img_w;
    *out_h = (H)img_h;
    return 0;
}

/* ── Native GIF Decoder ─────────────────────────────────────────────── */
int decode_gif_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h) {
    if (!filepath || !out_pixels || !out_w || !out_h) return -1;
    *out_pixels = NULL;
    *out_w = 0;
    *out_h = 0;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    UB hdr[13];
    if (fread(hdr, 1, 13, fp) != 13) {
        fclose(fp);
        return -1;
    }

    if (memcmp(hdr, "GIF87a", 6) != 0 && memcmp(hdr, "GIF89a", 6) != 0) {
        fclose(fp);
        return -1;
    }

    UB flags = hdr[10];
    int has_gct = (flags & 0x80) != 0;
    int gct_size = 1 << ((flags & 0x07) + 1);

    UW gct[256];
    memset(gct, 0, sizeof(gct));

    if (has_gct) {
        UB gct_raw[768];
        if (fread(gct_raw, 1, gct_size * 3, fp) != (size_t)(gct_size * 3)) {
            fclose(fp);
            return -1;
        }
        for (int i = 0; i < gct_size; i++) {
            UB r = gct_raw[i * 3 + 0];
            UB g = gct_raw[i * 3 + 1];
            UB b = gct_raw[i * 3 + 2];
            gct[i] = (r << 16) | (g << 8) | b;
        }
    }

    int trans_idx = -1;
    int img_w = 0, img_h = 0;
    BOOL img_read = FALSE;

    static UH s_gif_prefix[4096];
    static UB s_gif_suffix[4096];
    static UB s_gif_pixel_stack[4096 + 1];
    UB *gif_raw = (UB *)malloc(MAX_GIF_PIXELS);
    if (!gif_raw) {
        fclose(fp);
        return -1;
    }

    while (!feof(fp)) {
        int b = fgetc(fp);
        if (b == EOF || b == 0x3B) break;

        if (b == 0x21) {
            int ext_label = fgetc(fp);
            if (ext_label == 0xF9) {
                int blk_sz = fgetc(fp);
                if (blk_sz == 4) {
                    UB gce[4];
                    if (fread(gce, 1, 4, fp) == 4) {
                        if (gce[0] & 0x01) trans_idx = gce[3];
                    }
                }
                while (1) {
                    int sub_sz = fgetc(fp);
                    if (sub_sz <= 0 || sub_sz == EOF) break;
                    fseek(fp, sub_sz, SEEK_CUR);
                }
            } else {
                while (1) {
                    int sub_sz = fgetc(fp);
                    if (sub_sz <= 0 || sub_sz == EOF) break;
                    fseek(fp, sub_sz, SEEK_CUR);
                }
            }
        } else if (b == 0x2C) {
            UB desc[9];
            if (fread(desc, 1, 9, fp) != 9) break;

            img_w = desc[4] | (desc[5] << 8);
            img_h = desc[6] | (desc[7] << 8);
            UB iflags = desc[8];

            int has_lct = (iflags & 0x80) != 0;
            UW palette[256];
            memcpy(palette, gct, sizeof(palette));

            if (has_lct) {
                int lct_size = 1 << ((iflags & 0x07) + 1);
                UB lct_raw[768];
                if (fread(lct_raw, 1, lct_size * 3, fp) == (size_t)(lct_size * 3)) {
                    for (int i = 0; i < lct_size; i++) {
                        UB r = lct_raw[i * 3 + 0];
                        UB g = lct_raw[i * 3 + 1];
                        UB b = lct_raw[i * 3 + 2];
                        palette[i] = (r << 16) | (g << 8) | b;
                    }
                }
            } else if (!has_gct) {
                palette[0] = 0x00FFFFFF;
                palette[1] = 0x00000000;
            }

            int min_code_size = fgetc(fp);
            if (min_code_size < 2 || min_code_size > 8) min_code_size = 8;

            int clear_code = 1 << min_code_size;
            int eoi_code = clear_code + 1;
            int next_code = eoi_code + 1;
            int code_size = min_code_size + 1;
            int code_mask = (1 << code_size) - 1;

            for (int i = 0; i < clear_code; i++) {
                s_gif_prefix[i] = 0;
                s_gif_suffix[i] = (UB)i;
            }

            int bit_buf = 0;
            int bit_count = 0;
            int old_code = -1;
            int first_char = 0;
            int stack_top = 0;
            int pixel_count = 0;
            int total_pixels = img_w * img_h;

            while (pixel_count < total_pixels && pixel_count < MAX_GIF_PIXELS) {
                while (bit_count < code_size) {
                    int sub_sz = fgetc(fp);
                    if (sub_sz <= 0 || sub_sz == EOF) break;
                    UB sub_blk[256];
                    if (fread(sub_blk, 1, sub_sz, fp) != (size_t)sub_sz) break;
                    for (int bi = 0; bi < sub_sz; bi++) {
                        bit_buf |= ((int)sub_blk[bi]) << bit_count;
                        bit_count += 8;
                        while (bit_count >= code_size) {
                            int code = bit_buf & code_mask;
                            bit_buf >>= code_size;
                            bit_count -= code_size;

                            if (code == clear_code) {
                                code_size = min_code_size + 1;
                                code_mask = (1 << code_size) - 1;
                                next_code = eoi_code + 1;
                                old_code = -1;
                                continue;
                            }
                            if (code == eoi_code) goto gif_done_frame;

                            int incode = code;
                            if (code >= next_code) {
                                s_gif_pixel_stack[stack_top++] = (UB)first_char;
                                code = old_code;
                            }

                            while (code >= clear_code && code < 4096) {
                                s_gif_pixel_stack[stack_top++] = s_gif_suffix[code];
                                code = s_gif_prefix[code];
                            }
                            first_char = s_gif_suffix[code];
                            s_gif_pixel_stack[stack_top++] = (UB)first_char;

                            if (next_code < 4096 && old_code >= 0) {
                                s_gif_prefix[next_code] = (UH)old_code;
                                s_gif_suffix[next_code] = (UB)first_char;
                                next_code++;
                                if ((next_code & code_mask) == 0 && next_code < 4096) {
                                    code_size++;
                                    code_mask = (1 << code_size) - 1;
                                }
                            }
                            old_code = incode;

                            while (stack_top > 0 && pixel_count < total_pixels && pixel_count < MAX_GIF_PIXELS) {
                                gif_raw[pixel_count++] = s_gif_pixel_stack[--stack_top];
                            }
                        }
                    }
                }
                break;
            }

gif_done_frame:
            img_read = TRUE;

            /* Convert indexed pixels to RGBA */
            UB *out = (UB *)malloc((size_t)img_w * img_h * 4);
            if (!out) {
                free(gif_raw);
                fclose(fp);
                return -1;
            }

            for (int p = 0; p < total_pixels && p < MAX_GIF_PIXELS; p++) {
                UB idx = gif_raw[p];
                UB *dst = out + p * 4;
                if ((int)idx == trans_idx) {
                    dst[0] = 0;
                    dst[1] = 0;
                    dst[2] = 0;
                    dst[3] = 0;
                } else {
                    UW col = palette[idx];
                    dst[0] = (UB)((col >> 16) & 0xFF); /* R */
                    dst[1] = (UB)((col >> 8) & 0xFF);  /* G */
                    dst[2] = (UB)(col & 0xFF);         /* B */
                    dst[3] = 255;
                }
            }

            free(gif_raw);
            fclose(fp);
            *out_pixels = out;
            *out_w = (H)img_w;
            *out_h = (H)img_h;
            return 0;
        }
    }

    free(gif_raw);
    fclose(fp);
    return img_read ? 0 : -1;
}

int decode_image_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h) {
    if (!filepath) return -1;
    if (strstr(filepath, ".png") || strstr(filepath, ".PNG")) {
        return decode_png_rgba(filepath, out_pixels, out_w, out_h);
    }
    return decode_gif_rgba(filepath, out_pixels, out_w, out_h);
}

#else

int decode_png_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h) {
    (void)filepath; (void)out_pixels; (void)out_w; (void)out_h;
    return -1;
}

int decode_gif_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h) {
    (void)filepath; (void)out_pixels; (void)out_w; (void)out_h;
    return -1;
}

int decode_image_rgba(const char *filepath, UB **out_pixels, H *out_w, H *out_h) {
    (void)filepath; (void)out_pixels; (void)out_w; (void)out_h;
    return -1;
}

#endif
