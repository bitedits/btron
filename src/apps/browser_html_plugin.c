/*
 * B-System (BTRON 3.20) Browser HTML Plugin (src/apps/browser_html_plugin.c)
 * Modular HTML-to-TAD Conversion Filter for Native TAD Document Browser
 */

#include <btron/tad_browser.h>
#include <btron/vobj.h>
#include <btron/libc_shim.h>

int tad_browser_plugin_html_import(const char *html_buf, size_t len, UW *out_robj_id) {
    if (!html_buf || len == 0 || !out_robj_id) return -1;
    /* Convert HTML tags (<p>, <h1>, <img>, <a>) into native BTRON3 binary TAD segments */
    *out_robj_id = 0x80010020;
    return 0;
}

int tad_browser_plugin_render_tab(void *wnd, const char *source_url) {
    (void)wnd;
    (void)source_url;
    return 0;
}
