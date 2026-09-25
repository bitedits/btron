/*
 * SegUI Headless Screen Capturer
 * src/tools/capture_segui.c
 *
 * Renders every shipped SegUI screen into an off-screen GDEV and dumps it as
 * raw ARGB for scripts/update_segui_screens.py to turn into PNGs.  This is the
 * check that a segment table is right before any target runs it: the layout is
 * pure arithmetic on a profile, so a wrong budget is visible here as a wrong
 * picture, with no hardware, keypad or display in the loop.
 *
 * The screen set is the 480x272 panel - the smallest budget SegUI ships on,
 * and therefore the only frame that can prove a segment table is honest.
 *
 * Alongside each frame it prints the segment budget it spent: the height of
 * every band and the share of the screen the chrome took.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/segui.h>

static const char *RAW_DIR = "/tmp/segui_raw_screens";

static void dump_screen(GDEV *dev, SEGUI_APP *app, const char *file,
                        const char *label)
{
    char path[256];
    FILE *fp;
    int w = dev->width, h = dev->height;
    int i, chrome = 0;

    snprintf(path, sizeof(path), "%s/%s.raw", RAW_DIR, file);
    fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "Error: cannot write %s\n", path);
        return;
    }
    fwrite(&w, sizeof(int), 1, fp);
    fwrite(&h, sizeof(int), 1, fp);
    fwrite(dev->pixels, sizeof(COLOR), (size_t)w * (size_t)h, fp);
    fclose(fp);

    for (i = 0; i < SEG_ID_COUNT; i++)
        if (i != SEG_BODY && app->lay.band_used[i])
            chrome += app->lay.band[i].bottom - app->lay.band[i].top;

    printf("  [CAPTURED] %-28s %dx%d  %s", label, w, h, path);
    printf("  chrome %dpx (%d%%)", chrome, h > 0 ? chrome * 100 / h : 0);
    for (i = 0; i < app->prof->n_bands; i++) {
        SEG_ID id = app->prof->bands[i].id;
        printf("  %s=%d", segui_segment_name(id),
               app->lay.band_used[id]
                   ? app->lay.band[id].bottom - app->lay.band[id].top : 0);
    }
    printf("\n");
}

static void show(SEGUI_APP *app, GDEV *dev, const char *file, const char *label)
{
    RECT all = { 0, 0, dev->width, dev->height };

    /* cls_dev() closes a device, it does not clear one, so the frame is wiped
     * by painting it.  SegUI then repaints every pixel it owns anyway; this is
     * only here so an unwritten band shows up as black rather than as the
     * previous frame. */
    fill_rec(dev, &all, COLOR_BLACK);

    /* Layout first: it turns the profile's segment table into rectangles, and
     * every widget in the active tab claims its share of the body in the same
     * pass.  Painting then has no arithmetic left to do. */
    segui_layout(app, dev);
    segui_paint(app, dev);
    dump_screen(dev, app, file, label);
}

int main(void)
{
    static SEGUI_APP demo, phone;
    GDEV *dev;

    printf("===============================================================\n");
    printf("   SegUI (Segmentation UI) - Headless Screen Set, 480x272\n");
    printf("===============================================================\n");

    mkdir(RAW_DIR, 0777);

    dev = opn_dev(480, 272);
    if (!dev) {
        fprintf(stderr, "Fatal: failed to allocate the capture GDEV.\n");
        return 1;
    }

    /* ── SegUI Demo: the four-button-and-a-label window, per tab ──────── */

    segui_demo_build(&demo, segui_profile_for(dev->width, dev->height));
    {
        static const struct { int tab; const char *stem; const char *name; }
        tabs[] = {
            { 0, "home",    "SegUI"   },
            { 1, "widgets", "Widgets" },
            { 2, "theme",   "Theme"   },
            { 3, "info",    "Info"    },
            { 4, "apps",    "Apps"    }
        };
        size_t i;
        char file[64], label[96];
        for (i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) {
            demo.active_tab = tabs[i].tab;
            snprintf(file, sizeof(file), "segui_demo_%s", tabs[i].stem);
            snprintf(label, sizeof(label), "Demo / %s", tabs[i].name);
            show(&demo, dev, file, label);
        }
    }

    /* ── SegUI Phone: the four minimal screens ────────────────────────── */

    segui_phone_build(&phone, segui_profile_for(dev->width, dev->height));
    {
        static const struct { int screen; const char *stem; const char *name; }
        screens[] = {
            { SEGUI_SCREEN_CONTACTS, "contacts", "Contacts" },
            { SEGUI_SCREEN_CHAT,     "chat",     "Chat"     },
            { SEGUI_SCREEN_PEOPLE,   "people",   "People"   },
            { SEGUI_SCREEN_SETTINGS, "settings", "Settings" }
        };
        size_t i;
        char file[64], label[96];
        for (i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
            snprintf(file, sizeof(file), "segui_phone_%s", screens[i].stem);
            snprintf(label, sizeof(label), "Phone / %s", screens[i].name);
            segui_phone_show(&phone, screens[i].screen);
            show(&phone, dev, file, label);
        }
    }

    printf("===============================================================\n");
    printf(" Raw frames in %s\n", RAW_DIR);
    printf("===============================================================\n");

    cls_dev(dev);
    return 0;
}
