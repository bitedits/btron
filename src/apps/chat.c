/*
 * B-System (BTRON 3.20) Chat Application (Haiku Chat): src/apps/chat.c
 * Conforming to BTRON3 SPEC 3.20, TRON HMI, and CHAT.md
 */

#include <btron/chat.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/event.h>
#include <btron/libc_shim.h>
#if BTRON_HOSTED
#include <time.h>
#endif

/* Forward Declarations for XML Helpers in chat_xml.c */
extern int chat_xml_build_presence(const char *from, const char *to, const char *show, const char *status, char *out_xml, int max_len);
extern int chat_xml_build_message(const char *from, const char *to, const char *type, const char *body, const char *stamp, char *out_xml, int max_len);
extern BOOL chat_xml_parse_message(const char *xml_str, char *out_from, char *out_to, char *out_type, char *out_body, char *out_stamp);
extern BOOL chat_xml_parse_presence(const char *xml_str, char *out_from, char *out_to, char *out_show, char *out_status);

/* Global State */
static ChatClient *g_chat_clients = NULL;
static ID g_next_client_id = 1;
static unsigned int g_nick_counter = 0;

/* Component-Based Word-Nick Dictionaries */
static const char *k_nick_prefixes[] = {
    "Cyber", "Retro", "Neon", "Matrix", "Tron", "Bit",
    "Quantum", "Pixel", "Solar", "Hyper", "Silicon", "Vector",
    "Micro", "B-Free", "T-Kernel", "Alpha", "Voxel", "Shadow", "Data"
};
#define NUM_NICK_PREFIXES (sizeof(k_nick_prefixes) / sizeof(k_nick_prefixes[0]))

static const char *k_nick_roles[] = {
    "Samurai", "Hacker", "Pilot", "Monk", "Ninja", "Walker",
    "Ronin", "Knight", "Wizard", "Courier", "Guru", "Sensei",
    "Daemon", "Crafter", "Operator", "Agent", "Runner", "Voyager"
};
#define NUM_NICK_ROLES (sizeof(k_nick_roles) / sizeof(k_nick_roles[0]))

/* ── 1. Deterministic Nick Generator ── */
void chat_generate_random_nick(char *out_nick, int max_len) {
    if (!out_nick || max_len <= 0) return;
    if (g_nick_counter == 0) {
#if BTRON_HOSTED
        g_nick_counter = (unsigned int)time(NULL) ^ 0x5A5A;
#else
        g_nick_counter = 0x5A5A;
#endif
    }
    int p_idx = (g_nick_counter + 7) % NUM_NICK_PREFIXES;
    int r_idx = (g_nick_counter + 13) % NUM_NICK_ROLES;
    g_nick_counter += 19;

    snprintf(out_nick, max_len, "%s-%s", k_nick_prefixes[p_idx], k_nick_roles[r_idx]);
}

/* Helper to get current timestamp "HH:MM:SS" */
static void get_current_time_str(char *out_time, int max_len) {
#if BTRON_HOSTED
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    if (tm_info) {
        strftime(out_time, max_len, "%H:%M:%S", tm_info);
    } else {
        snprintf(out_time, max_len, "12:00:00");
    }
#else
    snprintf(out_time, max_len, "12:00:00");
#endif
}

/* ── 2. TRON IPC Pub/Sub Message Bus ── */
void chat_ipc_init(void) {
    /* Ready in-memory broker */
}

static void chat_client_init_defaults(ChatClient *client) {
    client->roster_count = 0;
    
    /* Default Initial Roster Items */
    ChatRosterItem *r1 = &client->roster[client->roster_count++];
    strncpy(r1->jid, "Sakamura-Sensei@btron.org", sizeof(r1->jid) - 1);
    strncpy(r1->nick, "Sakamura-Sensei", sizeof(r1->nick) - 1);
    strncpy(r1->group, "Developers", sizeof(r1->group) - 1);
    r1->status = CHAT_STATUS_ONLINE;
    strncpy(r1->status_text, "Designing TRON 3.20 HMI", sizeof(r1->status_text) - 1);

    ChatRosterItem *r2 = &client->roster[client->roster_count++];
    strncpy(r2->jid, "Tron-Daemon@btron.org", sizeof(r2->jid) - 1);
    strncpy(r2->nick, "Tron-Daemon", sizeof(r2->nick) - 1);
    strncpy(r2->group, "General", sizeof(r2->group) - 1);
    r2->status = CHAT_STATUS_ONLINE;
    strncpy(r2->status_text, "TRON IPC Kernel Service", sizeof(r2->status_text) - 1);

    ChatRosterItem *r3 = &client->roster[client->roster_count++];
    strncpy(r3->jid, "#btron-hall@conference.btron.org", sizeof(r3->jid) - 1);
    strncpy(r3->nick, "#btron-hall", sizeof(r3->nick) - 1);
    strncpy(r3->group, "MUC Rooms", sizeof(r3->group) - 1);
    r3->status = CHAT_STATUS_ONLINE;
    strncpy(r3->status_text, "B-System Public Townhall", sizeof(r3->status_text) - 1);
}

ChatClient* chat_ipc_register_client(const char *preferred_nick) {
    ChatClient *client = (ChatClient*)calloc(1, sizeof(ChatClient));
    if (!client) return NULL;

    client->client_id = g_next_client_id++;
    if (preferred_nick && strlen(preferred_nick) > 0) {
        strncpy(client->nick, preferred_nick, sizeof(client->nick) - 1);
    } else {
        chat_generate_random_nick(client->nick, sizeof(client->nick));
    }

    snprintf(client->server, sizeof(client->server), CHAT_DEFAULT_SERVER);
    snprintf(client->jid, sizeof(client->jid), "%s@%s", client->nick, client->server);
    client->status = CHAT_STATUS_ONLINE;
    snprintf(client->status_msg, sizeof(client->status_msg), "Online in B-System 3.20");
    client->connected = TRUE;

    chat_client_init_defaults(client);

    /* Insert into global client list */
    client->next = g_chat_clients;
    g_chat_clients = client;

    /* Broadcast initial presence */
    chat_ipc_broadcast_presence(client);

    /* Broadcast MUC join announcement */
    char join_announcement[128];
    snprintf(join_announcement, sizeof(join_announcement), "--> %s has entered the room.", client->nick);
    chat_ipc_broadcast_system(CHAT_DEFAULT_ROOM, join_announcement);

    return client;
}

void chat_ipc_unregister_client(ChatClient *client) {
    if (!client) return;

    /* Broadcast part announcement */
    char part_announcement[128];
    snprintf(part_announcement, sizeof(part_announcement), "<-- %s has left the room.", client->nick);
    chat_ipc_broadcast_system(CHAT_DEFAULT_ROOM, part_announcement);

    client->status = CHAT_STATUS_OFFLINE;
    chat_ipc_broadcast_presence(client);

    /* Remove from client list */
    if (g_chat_clients == client) {
        g_chat_clients = client->next;
    } else {
        ChatClient *curr = g_chat_clients;
        while (curr && curr->next != client) {
            curr = curr->next;
        }
        if (curr) curr->next = client->next;
    }

    free(client);
}

static void append_message_to_wnd(WND *wnd, const char *sender, const char *text, ChatMsgType type, COLOR col) {
    if (!wnd || !wnd->user_data) return;
    ChatWndContext *ctx = (ChatWndContext*)wnd->user_data;
    if (ctx->history_count >= CHAT_MAX_MSG_HISTORY) {
        /* Shift history buffer up */
        for (int i = 0; i < CHAT_MAX_MSG_HISTORY - 1; i++) {
            ctx->history[i] = ctx->history[i + 1];
        }
        ctx->history_count = CHAT_MAX_MSG_HISTORY - 1;
    }

    ChatMessage *msg = &ctx->history[ctx->history_count++];
    strncpy(msg->sender_nick, sender ? sender : "System", sizeof(msg->sender_nick) - 1);
    strncpy(msg->text, text ? text : "", sizeof(msg->text) - 1);
    get_current_time_str(msg->timestamp, sizeof(msg->timestamp));
    msg->type = type;
    msg->color = col;

    /* Auto-scroll to bottom */
    ctx->scroll_y = ctx->history_count > 12 ? (ctx->history_count - 12) * 18 : 0;
}

void chat_ipc_broadcast_presence(ChatClient *client) {
    if (!client) return;

    char xml[512];
    const char *show_str = (client->status == CHAT_STATUS_ONLINE) ? "online" :
                           (client->status == CHAT_STATUS_AWAY)   ? "away" :
                           (client->status == CHAT_STATUS_DND)    ? "dnd" : "offline";
    chat_xml_build_presence(client->jid, NULL, show_str, client->status_msg, xml, sizeof(xml));

    /* Update all other clients' rosters */
    ChatClient *c = g_chat_clients;
    while (c) {
        if (c != client) {
            /* Find or add client to c's roster */
            BOOL found = FALSE;
            for (int i = 0; i < c->roster_count; i++) {
                if (strcmp(c->roster[i].nick, client->nick) == 0) {
                    c->roster[i].status = client->status;
                    strncpy(c->roster[i].status_text, client->status_msg, sizeof(c->roster[i].status_text) - 1);
                    found = TRUE;
                    break;
                }
            }
            if (!found && c->roster_count < CHAT_MAX_ROSTER) {
                ChatRosterItem *item = &c->roster[c->roster_count++];
                strncpy(item->jid, client->jid, sizeof(item->jid) - 1);
                strncpy(item->nick, client->nick, sizeof(item->nick) - 1);
                strncpy(item->group, "General", sizeof(item->group) - 1);
                item->status = client->status;
                strncpy(item->status_text, client->status_msg, sizeof(item->status_text) - 1);
            }
        }
        c = c->next;
    }
}

void chat_ipc_send_muc_message(ChatClient *sender, const char *room, const char *text) {
    if (!sender || !text || strlen(text) == 0) return;

    char xml[512];
    char stamp[16];
    get_current_time_str(stamp, sizeof(stamp));
    chat_xml_build_message(sender->jid, room ? room : CHAT_DEFAULT_ROOM, "groupchat", text, stamp, xml, sizeof(xml));

    /* Multiplex message to all open MUC windows across all clients */
    ChatClient *c = g_chat_clients;
    while (c) {
        if (c->muc_wnd) {
            COLOR col = (c == sender) ? COLOR_BLUE : COLOR_BLACK;
            append_message_to_wnd(c->muc_wnd, sender->nick, text, CHAT_MSG_GROUPCHAT, col);
        }
        c = c->next;
    }

    /* Interactive Virtual Bot Replies */
    if (strstr(text, "hello") || strstr(text, "Hello") || strstr(text, "hi") || strstr(text, "Hi") || strstr(text, "spec")) {
        char reply[128];
        if (strstr(text, "spec")) {
            snprintf(reply, sizeof(reply), "Conforming strictly to BTRON3 SPEC 3.20 & TRON HMI guidelines!");
        } else {
            snprintf(reply, sizeof(reply), "Greetings @%s! Welcome to the B-System Chat Network.", sender->nick);
        }
        ChatClient *c2 = g_chat_clients;
        while (c2) {
            if (c2->muc_wnd) {
                append_message_to_wnd(c2->muc_wnd, "Sakamura-Sensei", reply, CHAT_MSG_GROUPCHAT, COLOR_NAVY);
            }
            c2 = c2->next;
        }
    }
}

void chat_ipc_send_private_message(ChatClient *sender, const char *target_nick, const char *text) {
    if (!sender || !target_nick || !text) return;

    char xml[512];
    char stamp[16];
    get_current_time_str(stamp, sizeof(stamp));
    char target_jid[64];
    snprintf(target_jid, sizeof(target_jid), "%s@%s", target_nick, CHAT_DEFAULT_SERVER);
    chat_xml_build_message(sender->jid, target_jid, "chat", text, stamp, xml, sizeof(xml));

    /* 1. Append to sender's private chat window */
    for (int i = 0; i < sender->private_wnd_count; i++) {
        WND *w = sender->private_wnds[i];
        if (w && w->user_data) {
            ChatWndContext *ctx = (ChatWndContext*)w->user_data;
            if (strcmp(ctx->target_room_or_nick, target_nick) == 0) {
                append_message_to_wnd(w, sender->nick, text, CHAT_MSG_CHAT, COLOR_BLUE);
                break;
            }
        }
    }

    /* 2. Dispatch to recipient client if active */
    ChatClient *recip = g_chat_clients;
    while (recip) {
        if (strcmp(recip->nick, target_nick) == 0) {
            /* Open private window for recipient if not open */
            WND *pw = NULL;
            for (int i = 0; i < recip->private_wnd_count; i++) {
                WND *w = recip->private_wnds[i];
                if (w && w->user_data) {
                    ChatWndContext *ctx = (ChatWndContext*)w->user_data;
                    if (strcmp(ctx->target_room_or_nick, sender->nick) == 0) {
                        pw = w;
                        break;
                    }
                }
            }
            if (!pw) {
                pw = open_chat_private_window(recip, sender->nick);
            }
            if (pw) {
                append_message_to_wnd(pw, sender->nick, text, CHAT_MSG_CHAT, COLOR_BLACK);
            }
            break;
        }
        recip = recip->next;
    }
}

void chat_ipc_broadcast_system(const char *room, const char *text) {
    (void)room;
    if (!text) return;
    ChatClient *c = g_chat_clients;
    while (c) {
        if (c->muc_wnd) {
            append_message_to_wnd(c->muc_wnd, "System", text, CHAT_MSG_SYSTEM, COLOR_DKGRAY);
        }
        c = c->next;
    }
}

void chat_ipc_poll(void) {
    /* Polling hook for background IPC task pump */
}

/* ── 3. Painting & UI Renderers ── */

/* 3.1 Main Roster Window Painting */
static void paint_chat_main_window(WND *wnd, GDEV *dev) {
    if (!dev || !wnd || !wnd->user_data) return;
    ChatWndContext *ctx = (ChatWndContext*)wnd->user_data;
    ChatClient *client = ctx->client;
    if (!client) return;

    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_LTGRAY);

    /* Menu Bar */
    RECT menu_bar = { 0, 0, dev->width, 22 };
    fill_rec(dev, &menu_bar, COLOR_LTGRAY);
    drw_lin(dev, 0, 21, dev->width, 21);
    drw_tc_string(dev, 8, 4, "Jabber", COLOR_BLACK, 0);
    drw_tc_string(dev, 60, 4, "Buddies", COLOR_BLACK, 0);
    drw_tc_string(dev, 120, 4, "View", COLOR_BLACK, 0);
    drw_tc_string(dev, 160, 4, "Help", COLOR_BLACK, 0);

    /* Identity & Status Card */
    RECT id_card = { 4, 26, dev->width - 4, 68 };
    fill_rec(dev, &id_card, COLOR_WHITE);
    drw_rec(dev, &id_card);

    drw_tc_string(dev, 8, 30, client->nick, COLOR_NAVY, 0);
    char server_info[64];
    snprintf(server_info, sizeof(server_info), "@%s (TRON IPC)", client->server);
    drw_tc_string(dev, 8 + strlen(client->nick) * 8 + 4, 30, server_info, COLOR_DKGRAY, 0);

    /* Status Selector Button */
    RECT stat_btn = { 8, 46, 120, 64 };
    fill_rec(dev, &stat_btn, COLOR_LTGRAY);
    drw_rec(dev, &stat_btn);
    COLOR stat_dot = (client->status == CHAT_STATUS_ONLINE) ? COLOR_GREEN :
                     (client->status == CHAT_STATUS_AWAY)   ? COLOR_GOLD :
                     (client->status == CHAT_STATUS_DND)    ? COLOR_RED : COLOR_GRAY;
    RECT dot = { 14, 52, 20, 58 };
    fill_rec(dev, &dot, stat_dot);
    const char *stat_name = (client->status == CHAT_STATUS_ONLINE) ? "Online" :
                            (client->status == CHAT_STATUS_AWAY)   ? "Away" :
                            (client->status == CHAT_STATUS_DND)    ? "Busy" : "Offline";
    drw_tc_string(dev, 26, 48, stat_name, COLOR_BLACK, 0);

    /* Roster View Box */
    RECT roster_box = { 4, 72, dev->width - 4, dev->height - 34 };
    fill_rec(dev, &roster_box, COLOR_WHITE);
    drw_rec(dev, &roster_box);

    int cur_y = 76;
    /* Draw Groups and Items */
    const char *groups[] = { "General", "Developers", "MUC Rooms" };
    for (int g = 0; g < 3; g++) {
        if (cur_y >= roster_box.bottom - 16) break;

        /* Group Header */
        RECT g_hdr = { 6, cur_y, dev->width - 6, cur_y + 18 };
        fill_rec(dev, &g_hdr, COLOR_LTGRAY);
        drw_rec(dev, &g_hdr);

        char g_title[64];
        snprintf(g_title, sizeof(g_title), "v %s", groups[g]);
        drw_tc_string(dev, 10, cur_y + 2, g_title, COLOR_NAVY, 0);
        cur_y += 20;

        /* Items in this group */
        for (int i = 0; i < client->roster_count; i++) {
            if (strcmp(client->roster[i].group, groups[g]) == 0) {
                if (cur_y >= roster_box.bottom - 16) break;

                /* Status dot */
                COLOR item_dot = (client->roster[i].status == CHAT_STATUS_ONLINE) ? COLOR_GREEN :
                                 (client->roster[i].status == CHAT_STATUS_AWAY)   ? COLOR_GOLD :
                                 (client->roster[i].status == CHAT_STATUS_DND)    ? COLOR_RED : COLOR_GRAY;
                RECT idot = { 14, cur_y + 4, 20, cur_y + 10 };
                fill_rec(dev, &idot, item_dot);

                /* Nick & Status text */
                drw_tc_string(dev, 24, cur_y + 1, client->roster[i].nick, COLOR_BLACK, 0);
                if (strlen(client->roster[i].status_text) > 0) {
                    drw_tc_string(dev, 24 + strlen(client->roster[i].nick) * 8 + 6, cur_y + 1,
                                  client->roster[i].status_text, COLOR_GRAY, 0);
                }
                cur_y += 18;
            }
        }
    }

    /* Bottom Action Toolbar */
    RECT b_add = { 4, dev->height - 28, 80, dev->height - 4 };
    fill_rec(dev, &b_add, COLOR_LTGRAY);
    drw_rec(dev, &b_add);
    drw_tc_string(dev, 8, dev->height - 24, "+ Buddy", COLOR_BLACK, 0);

    RECT b_muc = { 86, dev->height - 28, 160, dev->height - 4 };
    fill_rec(dev, &b_muc, COLOR_LTGRAY);
    drw_rec(dev, &b_muc);
    drw_tc_string(dev, 92, dev->height - 24, "# MUC Room", COLOR_BLACK, 0);

    RECT b_conn = { 166, dev->height - 28, dev->width - 4, dev->height - 4 };
    fill_rec(dev, &b_conn, client->connected ? COLOR_LTGRAY : COLOR_GOLD);
    drw_rec(dev, &b_conn);
    drw_tc_string(dev, 172, dev->height - 24, client->connected ? "Disconnect" : "Connect", COLOR_BLACK, 0);
}

/* 3.2 MUC Groupchat & Private Chat Window Painting */
static void paint_chat_conversation_window(WND *wnd, GDEV *dev) {
    if (!dev || !wnd || !wnd->user_data) return;
    ChatWndContext *ctx = (ChatWndContext*)wnd->user_data;
    ChatClient *client = ctx->client;
    if (!client) return;

    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_LTGRAY);

    BOOL is_muc = (ctx->wnd_type == CHAT_WND_MUC);
    int sidebar_w = is_muc ? 120 : 0;

    /* Topic / Target Header */
    RECT topic_r = { 4, 4, dev->width - 4, 26 };
    fill_rec(dev, &topic_r, COLOR_WHITE);
    drw_rec(dev, &topic_r);
    char topic_buf[128];
    if (is_muc) {
        snprintf(topic_buf, sizeof(topic_buf), "MUC: %s | Topic: Welcome to B-System 3.20 BeOS Chat!", ctx->target_room_or_nick);
    } else {
        snprintf(topic_buf, sizeof(topic_buf), "Private Chat with: %s@btron.org", ctx->target_room_or_nick);
    }
    drw_tc_string(dev, 8, 7, topic_buf, COLOR_NAVY, 0);

    /* Transcript Box */
    RECT trans_box = { 4, 30, dev->width - sidebar_w - 4, dev->height - 38 };
    fill_rec(dev, &trans_box, COLOR_WHITE);
    drw_rec(dev, &trans_box);

    /* Render message history */
    int start_y = trans_box.top + 4;
    int visible_lines = (trans_box.bottom - trans_box.top - 8) / 18;
    int start_idx = (ctx->history_count > visible_lines) ? (ctx->history_count - visible_lines) : 0;

    for (int i = start_idx; i < ctx->history_count; i++) {
        ChatMessage *m = &ctx->history[i];
        char line_hdr[64];
        if (m->type == CHAT_MSG_SYSTEM) {
            snprintf(line_hdr, sizeof(line_hdr), "* %s", m->text);
            drw_tc_string(dev, trans_box.left + 6, start_y, line_hdr, COLOR_DKGRAY, 0);
        } else {
            snprintf(line_hdr, sizeof(line_hdr), "[%s] <%s>", m->timestamp, m->sender_nick);
            drw_tc_string(dev, trans_box.left + 6, start_y, line_hdr, m->color, 0);
            int hdr_len = strlen(line_hdr) * 8 + 8;
            drw_tc_string(dev, trans_box.left + 6 + hdr_len, start_y, m->text, COLOR_BLACK, 0);
        }
        start_y += 18;
    }

    /* MUC Participants Sidebar */
    if (is_muc) {
        RECT side_r = { dev->width - sidebar_w, 30, dev->width - 4, dev->height - 38 };
        fill_rec(dev, &side_r, COLOR_WHITE);
        drw_rec(dev, &side_r);

        RECT side_hdr = { side_r.left, side_r.top, side_r.right, side_r.top + 18 };
        fill_rec(dev, &side_hdr, COLOR_LTGRAY);
        drw_rec(dev, &side_hdr);
        drw_tc_string(dev, side_hdr.left + 4, side_hdr.top + 2, "Chatters (MUC)", COLOR_NAVY, 0);

        int sy = side_r.top + 22;
        /* Draw current active participants */
        ChatClient *curr = g_chat_clients;
        while (curr && sy < side_r.bottom - 16) {
            COLOR dot_col = (curr->status == CHAT_STATUS_ONLINE) ? COLOR_GREEN : COLOR_GOLD;
            RECT d = { side_r.left + 6, sy + 4, side_r.left + 12, sy + 10 };
            fill_rec(dev, &d, dot_col);
            drw_tc_string(dev, side_r.left + 16, sy + 1, curr->nick, COLOR_BLACK, 0);
            sy += 18;
            curr = curr->next;
        }
        /* Virtual bot chatters */
        if (sy < side_r.bottom - 16) {
            RECT d = { side_r.left + 6, sy + 4, side_r.left + 12, sy + 10 };
            fill_rec(dev, &d, COLOR_GREEN);
            drw_tc_string(dev, side_r.left + 16, sy + 1, "Sakamura-Sensei", COLOR_NAVY, 0);
            sy += 18;
        }
        if (sy < side_r.bottom - 16) {
            RECT d = { side_r.left + 6, sy + 4, side_r.left + 12, sy + 10 };
            fill_rec(dev, &d, COLOR_GREEN);
            drw_tc_string(dev, side_r.left + 16, sy + 1, "Tron-Daemon", COLOR_DKGRAY, 0);
        }
    }

    /* Message Composition Box */
    RECT inp_box = { 4, dev->height - 32, dev->width - 70, dev->height - 4 };
    fill_rec(dev, &inp_box, COLOR_WHITE);
    drw_rec(dev, &inp_box);

    if (ctx->input_len > 0) {
        drw_tc_string(dev, inp_box.left + 6, inp_box.top + 6, ctx->input_buf, COLOR_BLACK, 0);
        /* Blinking cursor */
        int cur_x = inp_box.left + 6 + ctx->input_len * 8;
        drw_lin(dev, cur_x, inp_box.top + 4, cur_x, inp_box.bottom - 4);
    } else {
        drw_tc_string(dev, inp_box.left + 6, inp_box.top + 6, "Type message and press Enter...", COLOR_GRAY, 0);
    }

    /* Send Button */
    RECT send_btn = { dev->width - 64, dev->height - 32, dev->width - 4, dev->height - 4 };
    fill_rec(dev, &send_btn, COLOR_LTGRAY);
    drw_rec(dev, &send_btn);
    drw_tc_string(dev, send_btn.left + 12, send_btn.top + 6, "Send", COLOR_BLACK, 0);
}

/* 3.3 Add Buddy Dialog Painting */
static void paint_chat_dialog_window(WND *wnd, GDEV *dev) {
    if (!dev || !wnd || !wnd->user_data) return;
    ChatWndContext *ctx = (ChatWndContext*)wnd->user_data;

    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_LTGRAY);

    drw_tc_string(dev, 12, 12, "Add New Buddy to Roster", COLOR_NAVY, 0);

    drw_tc_string(dev, 12, 40, "Jabber ID (JID):", COLOR_BLACK, 0);
    RECT f1 = { 12, 56, dev->width - 12, 78 };
    fill_rec(dev, &f1, COLOR_WHITE);
    drw_rec(dev, &f1);
    drw_tc_string(dev, 16, 60, ctx->field_jid[0] ? ctx->field_jid : "user@btron.org", ctx->field_jid[0] ? COLOR_BLACK : COLOR_GRAY, 0);

    drw_tc_string(dev, 12, 86, "Nickname:", COLOR_BLACK, 0);
    RECT f2 = { 12, 102, dev->width - 12, 124 };
    fill_rec(dev, &f2, COLOR_WHITE);
    drw_rec(dev, &f2);
    drw_tc_string(dev, 16, 106, ctx->field_nick[0] ? ctx->field_nick : "Handle", ctx->field_nick[0] ? COLOR_BLACK : COLOR_GRAY, 0);

    drw_tc_string(dev, 12, 132, "Group Category:", COLOR_BLACK, 0);
    RECT f3 = { 12, 148, dev->width - 12, 170 };
    fill_rec(dev, &f3, COLOR_WHITE);
    drw_rec(dev, &f3);
    drw_tc_string(dev, 16, 152, ctx->field_group[0] ? ctx->field_group : "General", COLOR_BLACK, 0);

    /* Buttons */
    RECT btn_add = { dev->width - 160, dev->height - 32, dev->width - 86, dev->height - 6 };
    fill_rec(dev, &btn_add, COLOR_LTGRAY);
    drw_rec(dev, &btn_add);
    drw_tc_string(dev, btn_add.left + 10, btn_add.top + 4, "Add", COLOR_BLACK, 0);

    RECT btn_cancel = { dev->width - 80, dev->height - 32, dev->width - 12, dev->height - 6 };
    fill_rec(dev, &btn_cancel, COLOR_LTGRAY);
    drw_rec(dev, &btn_cancel);
    drw_tc_string(dev, btn_cancel.left + 10, btn_cancel.top + 4, "Cancel", COLOR_BLACK, 0);
}

/* ── 4. Interactive Event Handlers ── */

static void handle_chat_main_event(WND *wnd, const EVT *ev) {
    if (!wnd || !ev || !wnd->user_data) return;
    ChatWndContext *ctx = (ChatWndContext*)wnd->user_data;
    ChatClient *client = ctx->client;
    if (!client) return;

    if (ev->type == EV_BUT_DOWN) {
        H mx = ev->pos.x;
        H my = ev->pos.y;

        /* Bottom Action Buttons */
        if (my >= wnd->client.bottom - 28 && my <= wnd->client.bottom - 4) {
            if (mx >= wnd->client.left + 4 && mx <= wnd->client.left + 80) {
                open_chat_add_buddy_dialog(client);
            } else if (mx >= wnd->client.left + 86 && mx <= wnd->client.left + 160) {
                open_chat_muc_window(client, CHAT_DEFAULT_ROOM);
            } else if (mx >= wnd->client.left + 166 && mx <= wnd->client.right - 4) {
                client->connected = !client->connected;
                client->status = client->connected ? CHAT_STATUS_ONLINE : CHAT_STATUS_OFFLINE;
                chat_ipc_broadcast_presence(client);
            }
        }
        /* Status Picker Toggle */
        else if (my >= wnd->client.top + 46 && my <= wnd->client.top + 64 &&
                 mx >= wnd->client.left + 8 && mx <= wnd->client.left + 120) {
            client->status = (client->status + 1) % 4;
            chat_ipc_broadcast_presence(client);
        }
        /* Double click or click on roster list item */
        else if (my >= wnd->client.top + 72 && my <= wnd->client.bottom - 34) {
            /* Quick launch MUC or private chat */
            open_chat_muc_window(client, CHAT_DEFAULT_ROOM);
        }
    }
}

static void handle_chat_conversation_event(WND *wnd, const EVT *ev) {
    if (!wnd || !ev || !wnd->user_data) return;
    ChatWndContext *ctx = (ChatWndContext*)wnd->user_data;
    ChatClient *client = ctx->client;
    if (!client) return;

    if (ev->type == EV_KEY_DOWN) {
        UW key = ev->key;
        if (key == '\r' || key == '\n') {
            if (ctx->input_len > 0) {
                if (ctx->wnd_type == CHAT_WND_MUC) {
                    chat_ipc_send_muc_message(client, ctx->target_room_or_nick, ctx->input_buf);
                } else {
                    chat_ipc_send_private_message(client, ctx->target_room_or_nick, ctx->input_buf);
                }
                ctx->input_buf[0] = '\0';
                ctx->input_len = 0;
                ctx->cursor_pos = 0;
            }
        } else if (key == '\b' || key == 0x7F) {
            if (ctx->input_len > 0) {
                ctx->input_buf[--ctx->input_len] = '\0';
            }
        } else if (key >= 32 && key <= 126 && ctx->input_len < CHAT_MAX_LINE_LEN - 1) {
            ctx->input_buf[ctx->input_len++] = (char)key;
            ctx->input_buf[ctx->input_len] = '\0';
        }
    } else if (ev->type == EV_BUT_DOWN) {
        H mx = ev->pos.x;
        H my = ev->pos.y;

        /* Send Button Click */
        if (mx >= wnd->client.right - 64 && mx <= wnd->client.right - 4 &&
            my >= wnd->client.bottom - 32 && my <= wnd->client.bottom - 4) {
            if (ctx->input_len > 0) {
                if (ctx->wnd_type == CHAT_WND_MUC) {
                    chat_ipc_send_muc_message(client, ctx->target_room_or_nick, ctx->input_buf);
                } else {
                    chat_ipc_send_private_message(client, ctx->target_room_or_nick, ctx->input_buf);
                }
                ctx->input_buf[0] = '\0';
                ctx->input_len = 0;
                ctx->cursor_pos = 0;
            }
        }
    }
}

static void handle_chat_dialog_event(WND *wnd, const EVT *ev) {
    if (!wnd || !ev || !wnd->user_data) return;
    ChatWndContext *ctx = (ChatWndContext*)wnd->user_data;
    ChatClient *client = ctx->client;
    if (!client) return;

    if (ev->type == EV_BUT_DOWN) {
        H mx = ev->pos.x;
        H my = ev->pos.y;

        /* Add Button */
        if (mx >= wnd->client.right - 160 && mx <= wnd->client.right - 86 &&
            my >= wnd->client.bottom - 32 && my <= wnd->client.bottom - 6) {
            if (client->roster_count < CHAT_MAX_ROSTER) {
                ChatRosterItem *item = &client->roster[client->roster_count++];
                strncpy(item->jid, ctx->field_jid[0] ? ctx->field_jid : "newuser@btron.org", sizeof(item->jid) - 1);
                strncpy(item->nick, ctx->field_nick[0] ? ctx->field_nick : "NewBuddy", sizeof(item->nick) - 1);
                strncpy(item->group, ctx->field_group[0] ? ctx->field_group : "General", sizeof(item->group) - 1);
                item->status = CHAT_STATUS_ONLINE;
                strncpy(item->status_text, "Added to Roster", sizeof(item->status_text) - 1);
            }
            cls_wnd(wnd);
        }
        /* Cancel Button */
        else if (mx >= wnd->client.right - 80 && mx <= wnd->client.right - 12 &&
                 my >= wnd->client.bottom - 32 && my <= wnd->client.bottom - 6) {
            cls_wnd(wnd);
        }
    }
}

/* ── 5. Window Launchers ── */

WND* open_chat_main_window(ChatClient *client) {
    if (!client) return NULL;
    if (client->roster_wnd) {
        top_wnd(client->roster_wnd);
        return client->roster_wnd;
    }

    char title[64];
    snprintf(title, sizeof(title), "Blabber Roster: %s", client->nick);
    WND *w = opn_wnd(title, 60, 40, 260, 440,
                     WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (!w) return NULL;

    ChatWndContext *ctx = (ChatWndContext*)calloc(1, sizeof(ChatWndContext));
    ctx->wnd_type = CHAT_WND_ROSTER;
    ctx->client = client;
    w->user_data = (VW)ctx;
    w->paint = paint_chat_main_window;
    w->event_handler = handle_chat_main_event;

    client->roster_wnd = w;
    return w;
}

WND* open_chat_muc_window(ChatClient *client, const char *room_name) {
    if (!client) return NULL;
    if (client->muc_wnd) {
        top_wnd(client->muc_wnd);
        return client->muc_wnd;
    }

    const char *rname = room_name ? room_name : CHAT_DEFAULT_ROOM;
    char title[64];
    snprintf(title, sizeof(title), "%s (MUC)", rname);

    WND *w = opn_wnd(title, 330, 40, 520, 360,
                     WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (!w) return NULL;

    ChatWndContext *ctx = (ChatWndContext*)calloc(1, sizeof(ChatWndContext));
    ctx->wnd_type = CHAT_WND_MUC;
    ctx->client = client;
    strncpy(ctx->target_room_or_nick, rname, sizeof(ctx->target_room_or_nick) - 1);
    w->user_data = (VW)ctx;
    w->paint = paint_chat_conversation_window;
    w->event_handler = handle_chat_conversation_event;

    client->muc_wnd = w;

    /* Add initial greeting */
    append_message_to_wnd(w, "System", "Welcome to B-System 3.20 BeOS Chatroom (#btron-hall)!", CHAT_MSG_SYSTEM, COLOR_DKGRAY);
    return w;
}

WND* open_chat_private_window(ChatClient *client, const char *target_nick) {
    if (!client || !target_nick) return NULL;

    char title[64];
    snprintf(title, sizeof(title), "Chat: %s", target_nick);

    WND *w = opn_wnd(title, 180, 120, 440, 320,
                     WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER);
    if (!w) return NULL;

    ChatWndContext *ctx = (ChatWndContext*)calloc(1, sizeof(ChatWndContext));
    ctx->wnd_type = CHAT_WND_PRIVATE;
    ctx->client = client;
    strncpy(ctx->target_room_or_nick, target_nick, sizeof(ctx->target_room_or_nick) - 1);
    w->user_data = (VW)ctx;
    w->paint = paint_chat_conversation_window;
    w->event_handler = handle_chat_conversation_event;

    if (client->private_wnd_count < 8) {
        client->private_wnds[client->private_wnd_count++] = w;
    }
    return w;
}

WND* open_chat_add_buddy_dialog(ChatClient *client) {
    if (!client) return NULL;

    WND *w = opn_wnd("Add Buddy (JID)", 140, 100, 320, 220,
                     WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!w) return NULL;

    ChatWndContext *ctx = (ChatWndContext*)calloc(1, sizeof(ChatWndContext));
    ctx->wnd_type = CHAT_WND_BUDDY;
    ctx->client = client;
    strncpy(ctx->field_group, "General", sizeof(ctx->field_group) - 1);
    w->user_data = (VW)ctx;
    w->paint = paint_chat_dialog_window;
    w->event_handler = handle_chat_dialog_event;

    return w;
}

WND* open_chat_preferences_dialog(ChatClient *client) {
    if (!client) return NULL;
    return open_chat_add_buddy_dialog(client);
}

/* Global Application Entry Point */
WND* launch_beos_chat(void) {
    chat_ipc_init();
    ChatClient *client = chat_ipc_register_client(NULL);
    if (!client) return NULL;

    WND *roster = open_chat_main_window(client);
    open_chat_muc_window(client, CHAT_DEFAULT_ROOM);
    return roster;
}
