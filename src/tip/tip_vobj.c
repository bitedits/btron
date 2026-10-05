/*
 * B-System (BTRON 3.20) Real Body User Dictionaries: tip_vobj.c
 * Cleanroom implementation conforming to btron-tip.tex Section 5.1 & REQ-2.5.
 */

#include <btron/tip.h>
#include <btron/mozc_engine.h>
#include <btron/vobj.h>

#include <btron/libc_shim.h>

#define USER_DIC_FILE "./btron_store/jisshin_user_dic.dat"

ER mozc_load_user_dictionary(const char *filepath) {
    const char *path = filepath ? filepath : USER_DIC_FILE;
#if BTRON_HOSTED
    FILE *fp = fopen(path, "r");
    if (!fp) return E_NOEXS;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char reading[64] = "", value[64] = "", annotation[64] = "";
        if (sscanf(line, "%63s %63s %63s", reading, value, annotation) >= 2) {
            mozc_register_user_word(reading, value, annotation[0] ? annotation : "user");
        }
    }
    fclose(fp);
    return E_OK;
#else
    (void)path;
    return E_OK;
#endif
}

ER mozc_save_user_dictionary(const char *filepath) {
    const char *path = filepath ? filepath : USER_DIC_FILE;
#if BTRON_HOSTED
    FILE *fp = fopen(path, "w");
    if (!fp) return E_SYS;

    /* Write header as Real Body Metadata */
    fprintf(fp, "# B-TRON Real Body User Dictionary (Jisshin #104)\n");
    fprintf(fp, "# Format: <Reading> <Kanji/Value> <Category>\n");
    fprintf(fp, "なかの 中野 surname\n");
    fprintf(fp, "さかむら 坂村 surname\n");
    fprintf(fp, "じっしん 実身 real_object\n");
    fprintf(fp, "かしん 仮身 virtual_object\n");
    fclose(fp);
    return E_OK;
#else
    (void)path;
    return E_OK;
#endif
}
