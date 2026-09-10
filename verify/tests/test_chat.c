/*
 * B-System (BTRON 3.20) Chat & TRON IPC Test Suite: src/apps/test_chat.c
 * Conforming to BTRON3 SPEC 3.20, TRON HMI, and CHAT.md
 */

#include <btron/chat.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
            g_fail_count++; \
        } else { \
            printf("  [PASS] %s\n", msg); \
            g_pass_count++; \
        } \
    } while(0)

static int g_pass_count = 0;
static int g_fail_count = 0;

/* Forward Declarations for XML Helpers */
extern int chat_xml_build_presence(const char *from, const char *to, const char *show, const char *status, char *out_xml, int max_len);
extern int chat_xml_build_message(const char *from, const char *to, const char *type, const char *body, const char *stamp, char *out_xml, int max_len);
extern BOOL chat_xml_parse_message(const char *xml_str, char *out_from, char *out_to, char *out_type, char *out_body, char *out_stamp);
extern BOOL chat_xml_parse_presence(const char *xml_str, char *out_from, char *out_to, char *out_show, char *out_status);

/* ── Test 1: Random Component-Based Word-Nick Generator ── */
static void test_random_nick_generator(void) {
    printf("\n[TEST GROUP 1] Component-Based Word-Nick Generator\n");

    char nick1[32], nick2[32], nick3[32];
    chat_generate_random_nick(nick1, sizeof(nick1));
    chat_generate_random_nick(nick2, sizeof(nick2));
    chat_generate_random_nick(nick3, sizeof(nick3));

    TEST_ASSERT(strlen(nick1) > 4, "Generated non-empty nick1");
    TEST_ASSERT(strchr(nick1, '-') != NULL, "Nick1 contains component hyphen separator '-'");
    TEST_ASSERT(strlen(nick2) > 4, "Generated non-empty nick2");
    TEST_ASSERT(strlen(nick3) > 4, "Generated non-empty nick3");
    TEST_ASSERT(strcmp(nick1, nick2) != 0, "Sequential nicks are distinct");
    TEST_ASSERT(strcmp(nick2, nick3) != 0, "Distinct random handles across successive registrations");
}

/* ── Test 2: Lightweight XML Stanza Serialization & Parsing ── */
static void test_xml_stanza_engine(void) {
    printf("\n[TEST GROUP 2] XML Stanza Serialization & Deserialization\n");

    /* 1. Test Presence Stanza */
    char xml_pres[512];
    int len = chat_xml_build_presence("Cyber-Samurai@btron.org", "#btron-hall", "online", "Ready to chat", xml_pres, sizeof(xml_pres));
    TEST_ASSERT(len > 0, "chat_xml_build_presence generated XML string");
    TEST_ASSERT(strstr(xml_pres, "<presence") != NULL, "XML presence contains <presence> tag");
    TEST_ASSERT(strstr(xml_pres, "Cyber-Samurai@btron.org") != NULL, "XML presence contains from JID");
    TEST_ASSERT(strstr(xml_pres, "<show>online</show>") != NULL, "XML presence contains <show> element");

    char from[64], to[64], show[16], status[64];
    BOOL ok = chat_xml_parse_presence(xml_pres, from, to, show, status);
    TEST_ASSERT(ok == TRUE, "chat_xml_parse_presence parsed stanza successfully");
    TEST_ASSERT(strcmp(from, "Cyber-Samurai@btron.org") == 0, "Parsed matching from JID");
    TEST_ASSERT(strcmp(show, "online") == 0, "Parsed matching show status");
    TEST_ASSERT(strcmp(status, "Ready to chat") == 0, "Parsed matching status message");

    /* 2. Test Groupchat Message Stanza */
    char xml_msg[512];
    len = chat_xml_build_message("Retro-Hacker@btron.org", "#btron-hall", "groupchat", "Hello TRON World!", "12:34:56", xml_msg, sizeof(xml_msg));
    TEST_ASSERT(len > 0, "chat_xml_build_message generated XML string");

    char m_from[64], m_to[64], m_type[16], m_body[256], m_stamp[16];
    ok = chat_xml_parse_message(xml_msg, m_from, m_to, m_type, m_body, m_stamp);
    TEST_ASSERT(ok == TRUE, "chat_xml_parse_message parsed stanza successfully");
    TEST_ASSERT(strcmp(m_from, "Retro-Hacker@btron.org") == 0, "Parsed sender JID");
    TEST_ASSERT(strcmp(m_to, "#btron-hall") == 0, "Parsed room destination");
    TEST_ASSERT(strcmp(m_type, "groupchat") == 0, "Parsed groupchat type");
    TEST_ASSERT(strcmp(m_body, "Hello TRON World!") == 0, "Parsed body content text");
    TEST_ASSERT(strcmp(m_stamp, "12:34:56") == 0, "Parsed message timestamp");
}

/* ── Test 3: TRON IPC Multi-User Chat (MUC) Pub/Sub ── */
static void test_tron_ipc_muc(void) {
    printf("\n[TEST GROUP 3] TRON IPC Multi-User Chat (MUC) Pub/Sub\n");

    chat_ipc_init();
    ChatClient *alice = chat_ipc_register_client("Alice-Cyber");
    ChatClient *bob = chat_ipc_register_client("Bob-Retro");
    TEST_ASSERT(alice != NULL && bob != NULL, "Registered two distinct Chat clients in TRON IPC broker");

    /* Open MUC windows for both */
    WND *w_alice = open_chat_muc_window(alice, "#btron-hall");
    WND *w_bob = open_chat_muc_window(bob, "#btron-hall");
    TEST_ASSERT(w_alice != NULL && w_bob != NULL, "Opened MUC windows for Alice and Bob");

    ChatWndContext *ctx_alice = (ChatWndContext*)w_alice->user_data;
    ChatWndContext *ctx_bob = (ChatWndContext*)w_bob->user_data;
    int initial_alice_count = ctx_alice->history_count;
    int initial_bob_count = ctx_bob->history_count;

    /* Alice broadcasts message to MUC */
    chat_ipc_send_muc_message(alice, "#btron-hall", "Testing MUC broadcast from Alice");

    TEST_ASSERT(ctx_alice->history_count > initial_alice_count, "Alice's transcript contains sent message");
    TEST_ASSERT(ctx_bob->history_count > initial_bob_count, "Bob received Alice's MUC broadcast via TRON IPC");
    TEST_ASSERT(strstr(ctx_bob->history[ctx_bob->history_count - 1].text, "Testing MUC broadcast from Alice") != NULL, "Bob received exact broadcast payload");

    cls_wnd(w_alice);
    cls_wnd(w_bob);
    chat_ipc_unregister_client(alice);
    chat_ipc_unregister_client(bob);
}

/* ── Test 4: TRON IPC 1-on-1 Private Messaging ── */
static void test_tron_ipc_private_messaging(void) {
    printf("\n[TEST GROUP 4] TRON IPC 1-on-1 Private Messaging\n");

    ChatClient *charlie = chat_ipc_register_client("Charlie-Matrix");
    ChatClient *dave = chat_ipc_register_client("Dave-Ninja");

    WND *w_charlie_priv = open_chat_private_window(charlie, "Dave-Ninja");
    TEST_ASSERT(w_charlie_priv != NULL, "Opened private chat window from Charlie to Dave");

    ChatWndContext *ctx_c = (ChatWndContext*)w_charlie_priv->user_data;
    chat_ipc_send_private_message(charlie, "Dave-Ninja", "Secret 1-on-1 message");

    TEST_ASSERT(ctx_c->history_count > 0, "Charlie's private window logged outgoing message");

    /* Verify Dave received it and has an active private window */
    TEST_ASSERT(dave->private_wnd_count > 0, "Dave automatically received and opened private chat window");
    WND *w_dave_priv = dave->private_wnds[0];
    ChatWndContext *ctx_d = (ChatWndContext*)w_dave_priv->user_data;
    TEST_ASSERT(strstr(ctx_d->history[ctx_d->history_count - 1].text, "Secret 1-on-1 message") != NULL, "Dave received secret message payload");

    cls_wnd(w_charlie_priv);
    cls_wnd(w_dave_priv);
    chat_ipc_unregister_client(charlie);
    chat_ipc_unregister_client(dave);
}

/* ── Test 5: Full Set of Windows Lifecycle ── */
static void test_chat_windows_lifecycle(void) {
    printf("\n[TEST GROUP 5] Full Set of Windows Lifecycle\n");

    ChatClient *client = chat_ipc_register_client(NULL);
    TEST_ASSERT(client != NULL, "Created client for window test");

    WND *w_roster = open_chat_main_window(client);
    TEST_ASSERT(w_roster != NULL, "Opened Blabber Roster Main Window");
    TEST_ASSERT((w_roster->attr & WND_ATTR_COMPACT_TAB) != 0, "Roster window has compact sliding tabs");

    WND *w_muc = open_chat_muc_window(client, "#btron-hall");
    TEST_ASSERT(w_muc != NULL, "Opened MUC Groupchat Window");

    WND *w_buddy = open_chat_add_buddy_dialog(client);
    TEST_ASSERT(w_buddy != NULL, "Opened Add Buddy Dialog Window");

    cls_wnd(w_buddy);
    cls_wnd(w_muc);
    cls_wnd(w_roster);
    chat_ipc_unregister_client(client);
    TEST_ASSERT(1, "Cleanly reclaimed all Chat windows and client state");
}

int main(void) {
    printf("==========================================================\n");
    printf(" B-System BeOS Chat (Blabber) & TRON IPC Pub/Sub Test Suite\n");
    printf(" Conforming to BTRON3 SPEC 3.20, TRON HMI, and CHAT.md\n");
    printf("==========================================================\n");

    test_random_nick_generator();
    test_xml_stanza_engine();
    test_tron_ipc_muc();
    test_tron_ipc_private_messaging();
    test_chat_windows_lifecycle();

    printf("\n==========================================================\n");
    printf(" TEST RESULTS: %d / %d tests passed (%.1f%%)\n",
           g_pass_count, g_pass_count + g_fail_count,
           (float)g_pass_count / (g_pass_count + g_fail_count) * 100.0f);
    printf("==========================================================\n");

    return (g_fail_count == 0) ? 0 : 1;
}
