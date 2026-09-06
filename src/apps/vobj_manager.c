/*
 * B-System (BTRON 3.20) BTRON Accessory: Real Body Cabinet & Virtual Body Explorer Window (vobj_manager)
 * Cleanroom implementation of Sakamura BTRON / BTRON3 Architecture & NASA JPL Scope.
 */

#include <btron/wnd.h>
#include <btron/vobj.h>
#include <btron/troncode.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/error.h>
#include <btron/tad_browser.h>
#include <btron/app_menu.h>
#include <btron/settings.h>
#include <btron/settings_icon.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
#define memset   tkl_memset
#define memcpy   tkl_memcpy
#define strlen   tkl_strlen
#define strcmp   tkl_strcmp
#define strncpy  tkl_strncpy
extern int tkl_snprintf(char *str, size_t size, const char *format, ...);
#define snprintf tkl_snprintf

static inline char* local_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return (void*)0;
    size_t nlen = tkl_strlen(needle);
    if (nlen == 0) return (char*)haystack;
    while (*haystack) {
        if (tkl_memcmp(haystack, needle, nlen) == 0) return (char*)haystack;
        haystack++;
    }
    return (void*)0;
}
#define strstr local_strstr
#endif

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) WND* open_t_editor_window(void) {
    return (void*)0;
}
#else
extern WND* open_t_editor_window(void);
#endif

#define MAX_CABINET_ITEMS 128

typedef enum {
    CAB_VIEW_LIST = 0,
    CAB_VIEW_GRID = 1
} CAB_VIEW_MODE;

typedef struct {
    ID robj_id;
    VOBJ_TYPE type;
    char name[64];
    char path[128];
    UW size_bytes;
    const char *icon_tag;
    const char *category;
} CABINET_ITEM;

typedef struct {
    CABINET_ITEM items[MAX_CABINET_ITEMS];
    int item_count;
    int selected_idx;
    int hovered_idx;
    int scroll_offset;      /* Scroll item row offset */
    CAB_VIEW_MODE view_mode;
    char status_msg[128];

    /* In-Window Application Menu State */
    int active_menu;       /* -1 = closed, 0..4 = active header index */
    int hover_menu;        /* -1 = none, 0..4 = hovered header in closed state */
    int hover_item;        /* -1 = none, 0..N = hovered dropdown item */
    int sort_mode;         /* 0=Name, 1=Date/ID, 2=Size, 3=TOC/Type */
    APP_MENU_BAR menu_bar;
} CABINET_EXPLORER;

static CABINET_EXPLORER g_cabinet;


/* ── Natural String Comparison (Case-Insensitive & Numeric Aware) ───────── */
static int natural_compare(const char *s1, const char *s2) {
    if (!s1 || !s2) return 0;
    while (*s1 && *s2) {
        if (*s1 >= '0' && *s1 <= '9' && *s2 >= '0' && *s2 <= '9') {
            long n1 = 0, n2 = 0;
            while (*s1 >= '0' && *s1 <= '9') {
                n1 = n1 * 10 + (*s1 - '0');
                s1++;
            }
            while (*s2 >= '0' && *s2 <= '9') {
                n2 = n2 * 10 + (*s2 - '0');
                s2++;
            }
            if (n1 != n2) return (n1 < n2) ? -1 : 1;
        } else {
            char c1 = *s1;
            char c2 = *s2;
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) return (c1 < c2) ? -1 : 1;
            s1++;
            s2++;
        }
    }
    return (*s1 == '\0' && *s2 == '\0') ? 0 : (*s1 == '\0' ? -1 : 1);
}

/* ── Canonical Table of Contents (TOC) Sequence Weighting ───────────────── */
static int get_toc_order(const char *path, const char *name) {
    if (!path) return 1000;

    /* 1. Canonical Foundational Books & Main Portals */
    if (strstr(path, "01_btron3_spec")) return 10;
    if (strstr(path, "02_tkernel_book")) return 20;
    if (strstr(path, "03_bfree_os_book")) return 30;
    if (strstr(path, "04_tron_hmi_book")) return 35;

    /* 2. Top-level portal index */
    if (name && strcmp(name, "index.tad") == 0 && strcmp(path, "tad_bin/index.tad") == 0) return 40;

    /* 3. [doc] Shared Data Specifications (Chapter 1..6) */
    if (strstr(path, "shared_data/data_type")) return 100;
    if (strstr(path, "shared_data/tron_code")) return 110;
    if (strstr(path, "shared_data/tad1"))      return 120;
    if (strstr(path, "shared_data/tad2"))      return 130;
    if (strstr(path, "shared_data/tad3"))      return 140;
    if (strstr(path, "shared_data/fd_format")) return 150;
    if (strstr(path, "shared_data/index.tad")) return 160;
    if (strstr(path, "shared_data/indexfig"))  return 170;

    /* 4. [doc] OS Spec Portals */
    if (strstr(path, "os_spec/index.tad"))     return 200;
    if (strstr(path, "os_spec/indexfig.tad"))  return 205;

    /* 5. [doc] μITRON 3.0 Real-Time Kernel Subsystems */
    if (strstr(path, "os_spec/kernel/kernel"))   return 210;
    if (strstr(path, "os_spec/kernel/proc"))     return 220;
    if (strstr(path, "os_spec/kernel/memory"))   return 230;
    if (strstr(path, "os_spec/kernel/file"))     return 240;
    if (strstr(path, "os_spec/kernel/event"))    return 250;
    if (strstr(path, "os_spec/kernel/clk"))      return 260;
    if (strstr(path, "os_spec/kernel/device"))   return 270;
    if (strstr(path, "os_spec/kernel/message"))  return 280;
    if (strstr(path, "os_spec/kernel/taskcomm")) return 290;
    if (strstr(path, "os_spec/kernel/system"))   return 300;
    if (strstr(path, "os_spec/kernel/gname"))    return 310;

    /* 6. [doc] 2D Display Primitives Graphics */
    if (strstr(path, "os_spec/dp/dp.tad"))           return 400;
    if (strstr(path, "os_spec/dp/basic_concept"))    return 410;
    if (strstr(path, "os_spec/dp/basic_func"))       return 420;
    if (strstr(path, "os_spec/dp/character_func"))   return 430;
    if (strstr(path, "os_spec/dp/figure_func"))      return 440;
    if (strstr(path, "os_spec/dp/pointer_func"))     return 450;
    if (strstr(path, "os_spec/dp/intro"))            return 460;

    /* 7. [doc] BTRON3 Graphical User Interface & Shell */
    if (strstr(path, "os_spec/shell/shell.tad"))     return 500;
    if (strstr(path, "os_spec/shell/window"))        return 510;
    if (strstr(path, "os_spec/shell/menu"))          return 520;
    if (strstr(path, "os_spec/shell/panel"))         return 530;
    if (strstr(path, "os_spec/shell/parts"))         return 540;
    if (strstr(path, "os_spec/shell/font_mgr"))      return 550;
    if (strstr(path, "os_spec/shell/printmgr"))      return 560;
    if (strstr(path, "os_spec/shell/tip"))           return 570;
    if (strstr(path, "os_spec/shell/tray"))          return 580;
    if (strstr(path, "os_spec/shell/tcpip"))         return 590;
    if (strstr(path, "os_spec/shell/omgr"))          return 600;
    if (strstr(path, "os_spec/shell/data"))          return 610;

    /* 8. [b-hmi] TRON HMI Design Guidelines & Parts Book */
    if (strstr(path, "b-hmi/index"))      return 640;
    if (strstr(path, "b-hmi/part1"))      return 650;
    if (strstr(path, "b-hmi/part2"))      return 660;
    if (strstr(path, "b-hmi/part_book"))  return 670;
    if (strstr(path, "b-hmi"))            return 680;

    /* 9. [b-free] B-Free OS Architecture & Manifesto */
    if (strstr(path, "b-free/manifest"))   return 700;
    if (strstr(path, "b-free/kernel"))     return 710;
    if (strstr(path, "b-free/posix"))      return 720;
    if (strstr(path, "b-free/btron"))      return 730;
    if (strstr(path, "b-free/boot_arch"))  return 740;
    if (strstr(path, "b-free/source_tree"))return 750;
    if (strstr(path, "b-free/index"))      return 760;

    /* 10. [b-system] B-System POSIX & VirtIO Specs */
    if (strstr(path, "b-system/virtio"))     return 800;
    if (strstr(path, "b-system/btron_spec")) return 810;
    if (strstr(path, "b-system/kernel"))     return 820;
    if (strstr(path, "b-system/license"))    return 830;
    if (strstr(path, "b-system/index"))      return 840;

    /* 11. [t-kernel] T-Kernel 2.0 Real-Time OS Specs */
    if (strstr(path, "t-kernel/tkernel_spec"))    return 900;
    if (strstr(path, "t-kernel/tkernel_startup")) return 910;
    if (strstr(path, "t-kernel/tkernel_qemu"))    return 920;
    if (strstr(path, "t-kernel/index") || strstr(path, "t-kernel/T-Kernel")) return 930;

    /* 12. [b-book] B-Book Developer's Manual (12 Subsystems) */
    if (strstr(path, "b-book/B-Book") || strstr(path, "b-book/index")) return 940;
    if (strstr(path, "b-book/kernel"))   return 941;
    if (strstr(path, "b-book/cores"))    return 942;
    if (strstr(path, "b-book/graphics")) return 943;
    if (strstr(path, "b-book/tip"))      return 944;
    if (strstr(path, "b-book/vobject"))  return 945;
    if (strstr(path, "b-book/window"))   return 946;
    if (strstr(path, "b-book/desktop"))  return 947;
    if (strstr(path, "b-book/hmi"))      return 948;
    if (strstr(path, "b-book/font"))     return 949;
    if (strstr(path, "b-book/settings")) return 950;
    if (strstr(path, "b-book/drivers"))  return 951;
    if (strstr(path, "b-book/apps"))     return 952;

    return 1000;
}

/* ── Sort Cabinet Items: Group by First Column (icon_tag) & TOC Order ──── */
static void cabinet_sort_items(CABINET_EXPLORER *cab) {
    if (!cab || cab->item_count <= 1) return;
    for (int i = 0; i < cab->item_count - 1; i++) {
        for (int j = i + 1; j < cab->item_count; j++) {
            int swap = 0;
            if (cab->sort_mode == 1) {
                /* Sort by Name */
                if (natural_compare(cab->items[i].name, cab->items[j].name) > 0) swap = 1;
            } else if (cab->sort_mode == 2) {
                /* Sort by ID / Date */
                if (cab->items[i].robj_id > cab->items[j].robj_id) swap = 1;
            } else if (cab->sort_mode == 3) {
                /* Sort by Size */
                if (cab->items[i].size_bytes < cab->items[j].size_bytes) swap = 1;
            } else {
                /* 0 = Default: Group by First Column (icon_tag) & TOC Order */
                int cmp_grp = strcmp(cab->items[i].icon_tag, cab->items[j].icon_tag);
                if (cmp_grp > 0) {
                    swap = 1;
                } else if (cmp_grp == 0) {
                    int order_i = get_toc_order(cab->items[i].path, cab->items[i].name);
                    int order_j = get_toc_order(cab->items[j].path, cab->items[j].name);
                    if (order_i > order_j) swap = 1;
                    else if (order_i == order_j && natural_compare(cab->items[i].name, cab->items[j].name) > 0) swap = 1;
                }
            }
            if (swap) {
                CABINET_ITEM tmp = cab->items[i];
                cab->items[i] = cab->items[j];
                cab->items[j] = tmp;
            }
        }
    }
}

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1

static ID deduce_robj_id(const char *path) {
    if (strstr(path, "01_btron3_spec")) return 101;
    if (strstr(path, "02_tkernel_book")) return 102;
    if (strstr(path, "03_bfree_os_book")) return 103;
    if (strstr(path, "04_tron_hmi_book")) return 104;
    if (strstr(path, "b-hmi/index.tad") || strstr(path, "b-hmi/B-HMI.tad")) return 105;
    if (strstr(path, "b-free/manifest.tad")) return 106;
    if (strstr(path, "b-system/virtio.tad")) return 107;
    if (strstr(path, "data_type.tad")) return 111;
    if (strstr(path, "tron_code.tad")) return 112;
    if (strstr(path, "tad1.tad")) return 113;
    if (strstr(path, "tad2.tad")) return 114;
    if (strstr(path, "tad3.tad")) return 115;
    if (strstr(path, "fd_format.tad")) return 116;
    if (strstr(path, "kernel/kernel.tad") || strstr(path, "b-book/kernel")) return 121;
    if (strstr(path, "kernel/proc.tad")) return 122;
    if (strstr(path, "kernel/memory.tad")) return 123;
    if (strstr(path, "dp/dp.tad")) return 131;
    if (strstr(path, "shell/shell.tad")) return 141;
    if (strstr(path, "shell/window.tad")) return 142;
    if (strstr(path, "indexfig.tad")) return 161;

    UW h = 5381;
    for (int i = 0; path[i]; i++) {
        h = ((h << 5) + h) + (UB)path[i];
    }
    return 100 + (h % 900);
}

static const char* deduce_toc_path(const char *path) {
    if (strstr(path, "01_btron3_spec") || strstr(path, "02_tkernel_book") ||
        strstr(path, "03_bfree_os_book") || strstr(path, "04_tron_hmi_book")) {
        return "books/";
    }
    if (strstr(path, "b-book/")) return "b-book/";
    if (strstr(path, "shared_data/")) return "shared_data/";
    if (strstr(path, "os_spec/kernel/")) return "os_spec/kernel/";
    if (strstr(path, "os_spec/dp/")) return "os_spec/dp/";
    if (strstr(path, "os_spec/shell/")) return "os_spec/shell/";
    if (strstr(path, "os_spec/")) return "os_spec/";
    if (strstr(path, "b-hmi/part1")) return "b-hmi/part1/";
    if (strstr(path, "b-hmi/part2")) return "b-hmi/part2/";
    if (strstr(path, "b-hmi/part_book")) return "b-hmi/parts/";
    if (strstr(path, "b-hmi/")) return "b-hmi/";
    if (strstr(path, "b-free/")) return "b-free/";
    if (strstr(path, "b-system/")) return "b-system/";
    if (strstr(path, "t-kernel/")) return "t-kernel/";
    return "root/";
}

static const char* deduce_icon_tag(const char *path) {
    if (strstr(path, "04_tron_hmi") || strstr(path, "b-hmi")) return "[b-hmi]";
    if (strstr(path, "03_bfree") || strstr(path, "b-free")) return "[b-free]";
    if (strstr(path, "b-system")) return "[b-system]";
    if (strstr(path, "02_tkernel") || strstr(path, "t-kernel")) return "[t-kernel]";
    if (strstr(path, "b-book")) return "[doc]";
    if (strstr(path, "01_btron3") || strstr(path, "shared_data") || strstr(path, "os_spec") || strstr(path, "doc")) return "[doc]";
    return "[doc]";
}

/* Check if directory contains any non-index .tad file */
static int dir_has_titled_tad(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    struct dirent *de;
    int found = 0;
    while ((de = readdir(d)) != NULL) {
        int len = strlen(de->d_name);
        if (len > 4 && strcmp(de->d_name + len - 4, ".tad") == 0 && strcmp(de->d_name, "index.tad") != 0) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

/* Deduce a human-friendly document title for Cabinet Explorer */
static void get_friendly_title(const char *sub_path, const char *filename, char *out_name, size_t max_len) {
    if (!sub_path || !filename || !out_name || max_len == 0) return;

    if (strcmp(filename, "index.tad") != 0) {
        strncpy(out_name, filename, max_len - 1);
        out_name[max_len - 1] = '\0';
        return;
    }

    if (strstr(sub_path, "b-book/kernel")) strncpy(out_name, "Kernel.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/cores")) strncpy(out_name, "Cores.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/graphics")) strncpy(out_name, "Graphics.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/tip")) strncpy(out_name, "Tip.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/vobject")) strncpy(out_name, "VObject.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/window")) strncpy(out_name, "Window.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/desktop")) strncpy(out_name, "Desktop.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/hmi")) strncpy(out_name, "HMI.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/font")) strncpy(out_name, "Font.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/settings")) strncpy(out_name, "Settings.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/drivers")) strncpy(out_name, "Drivers.tad", max_len - 1);
    else if (strstr(sub_path, "b-book/apps")) strncpy(out_name, "Apps.tad", max_len - 1);
    else if (strstr(sub_path, "b-book")) strncpy(out_name, "B-Book.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi/part1")) strncpy(out_name, "Part1.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi/part2")) strncpy(out_name, "Part2.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi/part_book")) strncpy(out_name, "Part_Book.tad", max_len - 1);
    else if (strstr(sub_path, "b-hmi")) strncpy(out_name, "B-HMI.tad", max_len - 1);
    else if (strstr(sub_path, "t-kernel")) strncpy(out_name, "T-Kernel.tad", max_len - 1);
    else if (strstr(sub_path, "b-system")) strncpy(out_name, "B-System.tad", max_len - 1);
    else if (strstr(sub_path, "b-free")) strncpy(out_name, "B-Free.tad", max_len - 1);
    else if (strstr(sub_path, "shared_data")) strncpy(out_name, "Shared_Data.tad", max_len - 1);
    else if (strstr(sub_path, "os_spec")) strncpy(out_name, "OS_Spec.tad", max_len - 1);
    else strncpy(out_name, "B-System_Portal.tad", max_len - 1);
    out_name[max_len - 1] = '\0';
}

/* ── Dynamic Recursive Filesystem Discovery (Discovered not Hardcoded) ── */
static void cabinet_discover_dir(CABINET_EXPLORER *cab, const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;

        char sub_path[256];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, de->d_name);

        struct stat st;
        if (stat(sub_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            cabinet_discover_dir(cab, sub_path);
        } else if (S_ISREG(st.st_mode)) {
            int len = strlen(de->d_name);
            if (len > 4 && strcmp(de->d_name + len - 4, ".tad") == 0) {
                /* If index.tad and a titled companion exists, skip index.tad to prevent duplicates */
                if (strcmp(de->d_name, "index.tad") == 0 && dir_has_titled_tad(dir_path)) {
                    continue;
                }

                if (cab->item_count < MAX_CABINET_ITEMS) {
                    CABINET_ITEM *it = &cab->items[cab->item_count++];
                    it->robj_id = deduce_robj_id(sub_path);
                    it->type = VOBJ_TYPE_TEXT;
                    get_friendly_title(sub_path, de->d_name, it->name, sizeof(it->name));
                    strncpy(it->path, sub_path, sizeof(it->path) - 1);
                    it->size_bytes = (UW)st.st_size;
                    it->icon_tag = deduce_icon_tag(sub_path);
                    it->category = deduce_toc_path(sub_path);
                }
            }
        }
    }
    closedir(d);
}
#endif

static const char* deduce_gif_icon(const CABINET_ITEM *it) {
    if (!it) return "tad_browser";
    if (it->type == VOBJ_TYPE_DRAW) return "paint";
    if (strstr(it->path, "shared_data")) return "notebook";
    if (strstr(it->path, "b-hmi")) return "appearance";
    if (strstr(it->path, "t-kernel") || strstr(it->path, "kernel")) return "system";
    if (strstr(it->path, "b-free")) return "workbench";
    return "tad_browser";
}

static void cabinet_init_defaults(CABINET_EXPLORER *cab) {
    memset(cab, 0, sizeof(CABINET_EXPLORER));
    cab->selected_idx = 0;
    cab->hovered_idx = -1;
    cab->scroll_offset = 0;
    cab->view_mode = CAB_VIEW_LIST;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* Dynamic discovery from tad_bin/ directory tree */
    cabinet_discover_dir(cab, "tad_bin");
#endif

    if (cab->item_count > 0) {
        /* Group by first column & sort by TOC order */
        cabinet_sort_items(cab);
        snprintf(cab->status_msg, sizeof(cab->status_msg),
                 "Cabinet Ready. Discovered %d Real Bodys across tad_bin/.", cab->item_count);
        return;
    }

    /* Fallback static list for freestanding embedded environments */
    int n = 0;
    cab->items[n++] = (CABINET_ITEM){ 101, VOBJ_TYPE_TEXT, "01_btron3_spec.tad", "tad_bin/01_btron3_spec.tad", 3996, "[doc]", "books/" };
    cab->items[n++] = (CABINET_ITEM){ 102, VOBJ_TYPE_TEXT, "02_tkernel_book.tad", "tad_bin/02_tkernel_book.tad", 1745, "[t-kernel]", "books/" };
    cab->items[n++] = (CABINET_ITEM){ 103, VOBJ_TYPE_TEXT, "03_bfree_os_book.tad", "tad_bin/03_bfree_os_book.tad", 1472, "[b-free]", "books/" };
    cab->items[n++] = (CABINET_ITEM){ 111, VOBJ_TYPE_TEXT, "01_data_type.tad", "tad_bin/shared_data/data_type.tad", 10022, "[doc]", "shared_data/" };
    cab->item_count = n;
    cabinet_sort_items(cab);
    strncpy(cab->status_msg, "Cabinet Ready (Embedded Static Fallback).", sizeof(cab->status_msg) - 1);
}

#define CMENU_HDR_COUNT     5
#define CMENU_HDR_FILE      0
#define CMENU_HDR_EDIT      1
#define CMENU_HDR_VIEW      2
#define CMENU_HDR_VOBJ      3
#define CMENU_HDR_HELP      4

#define CMENU_DROPDOWN_WIDTH 250
#define CMENU_ROW_HEIGHT    20

enum {
    CCMD_NONE = 0,
    /* File */
    CCMD_FILE_OPEN = 10,
    CCMD_FILE_VIEW_TAD,
    CCMD_FILE_NEW,
    CCMD_FILE_DUPLICATE,
    CCMD_FILE_DELETE,
    CCMD_FILE_CLOSE,
    /* Edit */
    CCMD_EDIT_SELECT_ALL = 20,
    CCMD_EDIT_DESELECT,
    CCMD_EDIT_RENAME,
    CCMD_EDIT_PROPERTIES,
    /* View */
    CCMD_VIEW_LIST = 30,
    CCMD_VIEW_GRID,
    CCMD_VIEW_SORT_NAME,
    CCMD_VIEW_SORT_DATE,
    CCMD_VIEW_SORT_SIZE,
    CCMD_VIEW_SORT_TYPE,
    CCMD_VIEW_REFRESH,
    /* VObj */
    CCMD_VOBJ_CREATE_LINK = 40,
    CCMD_VOBJ_MOVE,
    CCMD_VOBJ_GLOBAL_INDEX,
    /* Help */
    CCMD_HELP_ABOUT = 50,
    CCMD_HELP_GUIDE
};

static void cab_sync_menu_state(void) {
    g_cabinet.active_menu = g_cabinet.menu_bar.active_menu;
    g_cabinet.hover_menu = g_cabinet.menu_bar.hover_menu;
    g_cabinet.hover_item = g_cabinet.menu_bar.hover_item;
}

static void cab_init_menu_bar(void) {
    app_menu_init(&g_cabinet.menu_bar, APP_MENU_STYLE_CLASSIC_3D);

    int h0 = app_menu_add_header(&g_cabinet.menu_bar, "ファイル(F)", 104);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "実身を開く (Open)", "Enter", CCMD_FILE_OPEN, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "実身を閲覧 (View)", "Space", CCMD_FILE_VIEW_TAD, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "新規実身の作成 (New)", "Ctrl+N", CCMD_FILE_NEW, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h0);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "実身の複製 (Duplicate)", "Ctrl+D", CCMD_FILE_DUPLICATE, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h0);
    app_menu_add_item(&g_cabinet.menu_bar, h0, "閉じる (Close)", "Ctrl+W", CCMD_FILE_CLOSE, TRUE);

    int h1 = app_menu_add_header(&g_cabinet.menu_bar, "編集(E)", 72);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "すべて選択 (Select All)", "Ctrl+A", CCMD_EDIT_SELECT_ALL, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "選択解除 (Deselect)", "Esc", CCMD_EDIT_DESELECT, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h1);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "名称変更 (Rename)", "F2", CCMD_EDIT_RENAME, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h1, "属性 (Properties...)", "Alt+Enter", CCMD_EDIT_PROPERTIES, TRUE);

    int h2 = app_menu_add_header(&g_cabinet.menu_bar, "表示(V)", 72);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "一覧表示 (List View)", "Ctrl+1", CCMD_VIEW_LIST, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "アイコン表示 (Icon View)", "Ctrl+2", CCMD_VIEW_GRID, TRUE);
    app_menu_add_separator(&g_cabinet.menu_bar, h2);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "名前順で整列 (Sort Name)", "F6", CCMD_VIEW_SORT_NAME, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "日付順で整列 (Sort Date)", "F7", CCMD_VIEW_SORT_DATE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "サイズ順で整列 (Sort Size)", "F8", CCMD_VIEW_SORT_SIZE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "種類順で整列 (Sort Type)", "F9", CCMD_VIEW_SORT_TYPE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h2, "再走査・更新 (Rescan)", "F5", CCMD_VIEW_REFRESH, TRUE);

    int h3 = app_menu_add_header(&g_cabinet.menu_bar, "仮身(O)", 72);
    app_menu_add_item(&g_cabinet.menu_bar, h3, "仮身リンク作成 (New Link)", "Ctrl+L", CCMD_VOBJ_CREATE_LINK, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h3, "キャビネット移動 (Move...)", "", CCMD_VOBJ_MOVE, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h3, "総索引 (Global Index)", "Ctrl+I", CCMD_VOBJ_GLOBAL_INDEX, TRUE);

    int h4 = app_menu_add_header(&g_cabinet.menu_bar, "ヘルプ(H)", 88);
    app_menu_add_item(&g_cabinet.menu_bar, h4, "キャビネット について (About)", "", CCMD_HELP_ABOUT, TRUE);
    app_menu_add_item(&g_cabinet.menu_bar, h4, "実身・仮身モデル解説 (Guide)", "", CCMD_HELP_GUIDE, TRUE);

    cab_sync_menu_state();
}

WND* open_vobj_about_window(void) {
    return app_menu_create_about_dialog("Cabinet", "実身・仮身",
                                        "Cleanroom BTRON Object Manager",
                                        "Brought to B-System by 5HT",
                                        280, 180);
}

static void paint_vobj_manager(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);

    /* ── 1. In-Window Application Menu Bar (y = 0..21) ────────────────────────── */
    if (g_cabinet.menu_bar.header_count == 0) cab_init_menu_bar();
    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "%d 実身", g_cabinet.item_count);
    app_menu_set_right_text(&g_cabinet.menu_bar, count_buf);
    app_menu_paint_bar(&g_cabinet.menu_bar, dev);

    /* Quick View Mode Toggle Button on right */
    if (dev->width >= 560) {
        RECT view_btn = { 440, 1, 542, 20 };
        fill_rec(dev, &view_btn, COLOR_WHITE);
        drw_rec(dev, &view_btn);
        const char *v_lbl = (g_cabinet.view_mode == CAB_VIEW_LIST) ? "[一覧表示]" : "[アイコン表示]";
        drw_tc_string(dev, view_btn.left + 8, view_btn.top + 2, v_lbl, COLOR_BLUE, 0x00000000);
    }

    /* ── 2. Content Viewport (starts at y = 26) ────────────────────────────── */
    int start_y = 26;
    int client_h = dev->height - 48;
    int visible_rows = client_h / 22;
    if (visible_rows < 1) visible_rows = 1;

    if (g_cabinet.view_mode == CAB_VIEW_LIST) {
        for (int r_idx = 0; r_idx < visible_rows; r_idx++) {
            int item_idx = g_cabinet.scroll_offset + r_idx;
            if (item_idx >= g_cabinet.item_count) break;

            CABINET_ITEM *it = &g_cabinet.items[item_idx];
            int y = start_y + r_idx * 22;
            RECT row_r = { 4, y, dev->width - 4, y + 20 };

            BOOL is_sel = (g_cabinet.selected_idx == item_idx);
            BOOL is_hov = (g_cabinet.hovered_idx == item_idx);

            if (is_sel) {
                fill_rec(dev, &row_r, COLOR_NAVY);
            } else if (is_hov) {
                fill_rec(dev, &row_r, COLOR_LTGRAY);
            }

            COLOR txt_col = is_sel ? COLOR_WHITE : COLOR_BLACK;

            /* Icon badge */
            drw_tc_string(dev, 8, y + 2, (it->type == 1) ? "[文書]" : "[画像]", is_sel ? COLOR_WHITE : COLOR_BLUE, 0x00000000);

            /* Real Object Name */
            char name_str[80];
            snprintf(name_str, sizeof(name_str), "%s", it->name);
            drw_tc_string(dev, 60, y + 2, name_str, txt_col, 0x00000000);

            /* Real Object ID */
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "#%d", it->robj_id);
            drw_tc_string(dev, dev->width - 150, y + 2, id_str, is_sel ? COLOR_WHITE : COLOR_DKGRAY, 0x00000000);

            /* Byte Size */
            char size_str[16];
            snprintf(size_str, sizeof(size_str), "%u B", it->size_bytes);
            drw_tc_string(dev, dev->width - 80, y + 2, size_str, is_sel ? COLOR_WHITE : COLOR_DKGRAY, 0x00000000);
        }
    } else {
        /* Grid Icon View */
        BTRON_ICON_SIZE sz = appearance_get_icon_size();
        int icon_dim = (sz == BTRON_ICON_SIZE_32) ? 32 : 64;
        int col_w = (sz == BTRON_ICON_SIZE_32) ? 96 : 110;
        int row_h = (sz == BTRON_ICON_SIZE_32) ? 72 : 104;
        int cols = (dev->width - 16) / col_w;
        if (cols < 1) cols = 1;

        for (int i = 0; i < g_cabinet.item_count; i++) {
            CABINET_ITEM *it = &g_cabinet.items[i];
            int col = i % cols;
            int r_idx = i / cols;
            int x = 12 + col * col_w;
            int y = start_y + r_idx * row_h;

            if (y + row_h > dev->height - 24) break;

            BOOL is_sel = (g_cabinet.selected_idx == i);
            RECT box = { x, y, x + col_w - 8, y + row_h - 4 };

            if (is_sel) {
                fill_rec(dev, &box, COLOR_NAVY);
            } else {
                fill_rec(dev, &box, COLOR_WHITE);
                drw_rec(dev, &box);
            }

            COLOR fg = is_sel ? COLOR_WHITE : COLOR_BLACK;

            /* Icon box */
            int icn_box_w = icon_dim + 8;
            int icn_box_h = icon_dim + 8;
            RECT icn = { x + (col_w - 8 - icn_box_w) / 2, y + 4,
                         x + (col_w - 8 + icn_box_w) / 2, y + 4 + icn_box_h };
            fill_rec(dev, &icn, is_sel ? COLOR_WHITE : COLOR_LTGRAY);
            drw_rec(dev, &icn);

            const char *icon_id = deduce_gif_icon(it);
            int icn_x = icn.left + 4;
            int icn_y = icn.top + 4;
            draw_setting_gif_icon_scaled(dev, icon_id, icn_x, icn_y, icon_dim, icon_dim);

            /* Truncated Name */
            char short_name[14];
            strncpy(short_name, it->name, 12);
            short_name[12] = '\0';
            H text_y = y + icn_box_h + 6;
            drw_tc_string(dev, x + 4, text_y, short_name, fg, 0x00000000);
        }
    }

    /* ── 4. Status Bar Footer (Bottom: height-22 .. height) ────────────────── */
    RECT status_r = { 0, dev->height - 22, dev->width, dev->height };
    fill_rec(dev, &status_r, COLOR_LTGRAY);
    drw_lin(dev, 0, dev->height - 22, dev->width, dev->height - 22);

    char foot_text[128];
    snprintf(foot_text, sizeof(foot_text), "キャビネット: %d 実身 [tad_bin] | #%d: %s (%u B)",
             g_cabinet.item_count,
             g_cabinet.items[g_cabinet.selected_idx].robj_id,
             g_cabinet.items[g_cabinet.selected_idx].name,
             g_cabinet.items[g_cabinet.selected_idx].size_bytes);
    drw_tc_string(dev, 10, dev->height - 17, foot_text, COLOR_BLACK, 0x00000000);

    /* ── 5. Dropdown Menu Overlay ──────────────────────────────────────────── */
    if (g_cabinet.menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&g_cabinet.menu_bar, dev);
    }
}

static void handle_vobj_manager_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    H rel_x = evt->pos.x - (wnd->bounds.left + 4);
    H rel_y = evt->pos.y - (wnd->bounds.top + 26);

    if (evt->type == EV_MOUSE_MOVE) {
        if (app_menu_handle_mouse_move(&g_cabinet.menu_bar, rel_x, rel_y)) {
            cab_sync_menu_state();
            return;
        }
        cab_sync_menu_state();

        /* Item Hover (starts at y = 26) */
        int start_y = 26;
        int idx = -1;
        if (g_cabinet.view_mode == CAB_VIEW_GRID) {
            BTRON_ICON_SIZE sz = appearance_get_icon_size();
            int col_w = (sz == BTRON_ICON_SIZE_32) ? 96 : 110;
            int row_h = (sz == BTRON_ICON_SIZE_32) ? 72 : 104;
            H dev_w = wnd->dev ? wnd->dev->width : 560;
            int cols = (dev_w - 16) / col_w;
            if (cols < 1) cols = 1;
            int c = (rel_x - 12) / col_w;
            int r = (rel_y - start_y) / row_h;
            if (c >= 0 && c < cols && r >= 0 && rel_x >= 12 && rel_y >= start_y) {
                int calc = r * cols + c;
                if (calc < g_cabinet.item_count) idx = calc;
            }
        } else {
            int row = (rel_y - start_y) / 22;
            int calc = g_cabinet.scroll_offset + row;
            if (calc >= 0 && calc < g_cabinet.item_count) idx = calc;
        }
        g_cabinet.hovered_idx = idx;
        return;
    }

    if (evt->type == EV_BUT_DOWN) {
        /* Quick toggle button on right of Menu Bar */
        H dev_w = wnd->dev ? wnd->dev->width : 560;
        if (dev_w >= 560 && rel_y >= 0 && rel_y <= 21) {
            if (rel_x >= 440 && rel_x <= 542) {
                g_cabinet.view_mode = (g_cabinet.view_mode == CAB_VIEW_LIST) ? CAB_VIEW_GRID : CAB_VIEW_LIST;
                return;
            }
        }

        int cmd = 0, sub_idx = -1;
        if (app_menu_handle_mouse_down(&g_cabinet.menu_bar, rel_x, rel_y, &cmd, &sub_idx)) {
            cab_sync_menu_state();
            if (cmd != 0) {
                switch (cmd) {
                    case CCMD_FILE_OPEN:
                    case CCMD_FILE_VIEW_TAD:
                        if (g_cabinet.selected_idx >= 0 && g_cabinet.selected_idx < g_cabinet.item_count) {
                            open_tad_browser_window(g_cabinet.items[g_cabinet.selected_idx].path,
                                                   g_cabinet.items[g_cabinet.selected_idx].name);
                        }
                        return;
                    case CCMD_FILE_NEW:
                        if (open_t_editor_window) open_t_editor_window();
                        return;
                    case CCMD_FILE_CLOSE:
                        cls_wnd(wnd);
                        return;
                    case CCMD_EDIT_SELECT_ALL:
                        g_cabinet.selected_idx = 0;
                        return;
                    case CCMD_EDIT_DESELECT:
                        g_cabinet.selected_idx = -1;
                        return;
                    case CCMD_VIEW_LIST:
                        g_cabinet.view_mode = CAB_VIEW_LIST;
                        return;
                    case CCMD_VIEW_GRID:
                        g_cabinet.view_mode = CAB_VIEW_GRID;
                        return;
                    case CCMD_VIEW_SORT_NAME:
                        g_cabinet.sort_mode = 0;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_SORT_DATE:
                        g_cabinet.sort_mode = 1;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_SORT_SIZE:
                        g_cabinet.sort_mode = 2;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_SORT_TYPE:
                        g_cabinet.sort_mode = 3;
                        cabinet_sort_items(&g_cabinet);
                        return;
                    case CCMD_VIEW_REFRESH:
                        cabinet_init_defaults(&g_cabinet);
                        return;
                    case CCMD_HELP_ABOUT:
                        open_vobj_about_window();
                        return;
                    default:
                        return;
                }
            }
            return;
        }
        cab_sync_menu_state();

        /* C. Item Selection Click (starts at y = 26) */
        int start_y = 26;
        int idx = -1;
        if (g_cabinet.view_mode == CAB_VIEW_GRID) {
            BTRON_ICON_SIZE sz = appearance_get_icon_size();
            int col_w = (sz == BTRON_ICON_SIZE_32) ? 96 : 110;
            int row_h = (sz == BTRON_ICON_SIZE_32) ? 72 : 104;
            H dev_w = wnd->dev ? wnd->dev->width : 560;
            int cols = (dev_w - 16) / col_w;
            if (cols < 1) cols = 1;
            int c = (rel_x - 12) / col_w;
            int r = (rel_y - start_y) / row_h;
            if (c >= 0 && c < cols && r >= 0 && rel_x >= 12 && rel_y >= start_y) {
                int calc = r * cols + c;
                if (calc < g_cabinet.item_count) idx = calc;
            }
        } else {
            int row = (rel_y - start_y) / 22;
            int calc = g_cabinet.scroll_offset + row;
            if (calc >= 0 && calc < g_cabinet.item_count) idx = calc;
        }

        if (idx >= 0 && idx < g_cabinet.item_count) {
            static int s_last_click_idx = -1;
            static UW s_last_click_time = 0;
            UW cur_time = (UW)(uintptr_t)evt->data;
            BOOL is_double = (s_last_click_idx == idx && (cur_time - s_last_click_time < 400 || s_last_click_time == 0));

            g_cabinet.selected_idx = idx;
            s_last_click_idx = idx;
            s_last_click_time = cur_time;

            if (is_double && idx >= 0 && idx < g_cabinet.item_count) {
                open_tad_browser_window(g_cabinet.items[idx].path, g_cabinet.items[idx].name);
            }
        }
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        UW key = evt->key;
        if (key == BTRON_KEY_ESCAPE || key == 27) {
            int cmd = 0;
            if (app_menu_handle_key(&g_cabinet.menu_bar, key, (uint16_t)(uintptr_t)evt->data, &cmd)) {
                cab_sync_menu_state();
                return;
            }
            cab_sync_menu_state();
        }

        if (key == BTRON_KEY_UP || key == 'k') {
            if (g_cabinet.selected_idx > 0) {
                g_cabinet.selected_idx--;
                if (g_cabinet.selected_idx < g_cabinet.scroll_offset) {
                    g_cabinet.scroll_offset = g_cabinet.selected_idx;
                }
            }
        } else if (key == BTRON_KEY_DOWN || key == 'j') {
            if (g_cabinet.selected_idx < g_cabinet.item_count - 1) {
                g_cabinet.selected_idx++;
                int visible_rows = (wnd->dev ? wnd->dev->height - 52 : 240) / 22;
                if (g_cabinet.selected_idx >= g_cabinet.scroll_offset + visible_rows) {
                    g_cabinet.scroll_offset = g_cabinet.selected_idx - visible_rows + 1;
                }
            }
        } else if (key == BTRON_KEY_PAGE_UP) {
            g_cabinet.scroll_offset -= 8;
            if (g_cabinet.scroll_offset < 0) g_cabinet.scroll_offset = 0;
            g_cabinet.selected_idx = g_cabinet.scroll_offset;
        } else if (key == BTRON_KEY_PAGE_DOWN) {
            g_cabinet.scroll_offset += 8;
            if (g_cabinet.scroll_offset > g_cabinet.item_count - 1) g_cabinet.scroll_offset = g_cabinet.item_count - 1;
            g_cabinet.selected_idx = g_cabinet.scroll_offset;
        } else if (key == '\n' || key == '\r' || key == ' ') {
            if (g_cabinet.selected_idx >= 0 && g_cabinet.selected_idx < g_cabinet.item_count) {
                open_tad_browser_window(g_cabinet.items[g_cabinet.selected_idx].path,
                                       g_cabinet.items[g_cabinet.selected_idx].name);
            }
        }
    }
}

BOOL cabinet_handle_click(int mouse_x, int mouse_y, BOOL is_double_click, ID *out_robj_id, char *out_path) {
    (void)mouse_x;
    if (mouse_y >= 28 && mouse_y <= 52) {
        /* Toolbar click */
        if (mouse_x >= 275 && mouse_x <= 375) {
            g_cabinet.view_mode = (g_cabinet.view_mode == CAB_VIEW_LIST) ? CAB_VIEW_GRID : CAB_VIEW_LIST;
            return FALSE;
        } else if (mouse_x >= 380 && mouse_x <= 500) {
            cabinet_init_defaults(&g_cabinet);
            return FALSE;
        }
    }

    int start_y = 60;
    int row = (mouse_y - start_y) / 22;
    int idx = g_cabinet.scroll_offset + row;
    if (idx >= 0 && idx < g_cabinet.item_count) {
        g_cabinet.selected_idx = idx;
        if (out_robj_id) *out_robj_id = g_cabinet.items[idx].robj_id;
        if (out_path) strncpy(out_path, g_cabinet.items[idx].path, 127);

        if (is_double_click) {
            open_tad_browser_window(g_cabinet.items[idx].path, g_cabinet.items[idx].name);
            return TRUE;
        }
    }
    return FALSE;
}

WND* open_vobj_manager_window(void) {
    if (g_cabinet.item_count == 0) {
        cabinet_init_defaults(&g_cabinet);
    }
    WND *wnd = opn_wnd("実身キャビネット (Cabinet Explorer)", 80, 60, 560, 360,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (wnd) {
        wnd->paint = paint_vobj_manager;
        wnd->event_handler = handle_vobj_manager_event;
    }
    return wnd;
}
