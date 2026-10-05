/*
 * B-System (BTRON 3.20) Chat XML Engine: src/apps/chat_xml.c
 * Cleanroom C99 implementation of BeOS Chat XMLEntity & Stanza processor.
 * Conforming to CHAT.md and NASA JPL deterministic execution rules.
 */

#include <btron/chat.h>
#include <btron/libc_shim.h>

#define MAX_XML_ATTRS    8
#define MAX_XML_CHILDREN 16
#define MAX_XML_TAG_LEN  32
#define MAX_XML_VAL_LEN  128
#define MAX_XML_DATA_LEN 512

typedef struct {
    char key[MAX_XML_TAG_LEN];
    char val[MAX_XML_VAL_LEN];
} ChatXmlAttr;

typedef struct ChatXmlNode {
    char tag[MAX_XML_TAG_LEN];
    ChatXmlAttr attrs[MAX_XML_ATTRS];
    int attr_count;
    char cdata[MAX_XML_DATA_LEN];
    struct ChatXmlNode *children[MAX_XML_CHILDREN];
    int child_count;
} ChatXmlNode;

/* XML Node Memory Allocation */
ChatXmlNode* chat_xml_node_alloc(const char *tag) {
    ChatXmlNode *node = (ChatXmlNode*)calloc(1, sizeof(ChatXmlNode));
    if (!node) return NULL;
    if (tag) {
        strncpy(node->tag, tag, MAX_XML_TAG_LEN - 1);
    }
    return node;
}

void chat_xml_free(ChatXmlNode *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        chat_xml_free(node->children[i]);
    }
    free(node);
}

void chat_xml_add_attr(ChatXmlNode *node, const char *key, const char *val) {
    if (!node || !key || !val || node->attr_count >= MAX_XML_ATTRS) return;
    strncpy(node->attrs[node->attr_count].key, key, MAX_XML_TAG_LEN - 1);
    strncpy(node->attrs[node->attr_count].val, val, MAX_XML_VAL_LEN - 1);
    node->attr_count++;
}

const char* chat_xml_get_attr(const ChatXmlNode *node, const char *key) {
    if (!node || !key) return NULL;
    for (int i = 0; i < node->attr_count; i++) {
        if (strcmp(node->attrs[i].key, key) == 0) {
            return node->attrs[i].val;
        }
    }
    return NULL;
}

void chat_xml_add_child(ChatXmlNode *parent, ChatXmlNode *child) {
    if (!parent || !child || parent->child_count >= MAX_XML_CHILDREN) return;
    parent->children[parent->child_count++] = child;
}

ChatXmlNode* chat_xml_get_child(const ChatXmlNode *node, const char *tag) {
    if (!node || !tag) return NULL;
    for (int i = 0; i < node->child_count; i++) {
        if (node->children[i] && strcmp(node->children[i]->tag, tag) == 0) {
            return node->children[i];
        }
    }
    return NULL;
}

/* Format node tree to XML string */
int chat_xml_format(const ChatXmlNode *node, char *out_buf, int max_len) {
    if (!node || !out_buf || max_len <= 0) return 0;

    char header[256];
    snprintf(header, sizeof(header), "<%s", node->tag);
    for (int i = 0; i < node->attr_count; i++) {
        char attr_buf[128];
        snprintf(attr_buf, sizeof(attr_buf), " %s=\"%s\"", node->attrs[i].key, node->attrs[i].val);
        strncat(header, attr_buf, sizeof(header) - strlen(header) - 1);
    }

    if (node->child_count == 0 && strlen(node->cdata) == 0) {
        strncat(header, "/>", sizeof(header) - strlen(header) - 1);
        strncpy(out_buf, header, max_len - 1);
        out_buf[max_len - 1] = '\0';
        return strlen(out_buf);
    }

    strncat(header, ">", sizeof(header) - strlen(header) - 1);
    strncpy(out_buf, header, max_len - 1);
    out_buf[max_len - 1] = '\0';

    if (strlen(node->cdata) > 0) {
        strncat(out_buf, node->cdata, max_len - strlen(out_buf) - 1);
    }

    for (int i = 0; i < node->child_count; i++) {
        char child_str[512];
        chat_xml_format(node->children[i], child_str, sizeof(child_str));
        strncat(out_buf, child_str, max_len - strlen(out_buf) - 1);
    }

    char footer[64];
    snprintf(footer, sizeof(footer), "</%s>", node->tag);
    strncat(out_buf, footer, max_len - strlen(out_buf) - 1);

    return strlen(out_buf);
}

/* Stanza Builder Helpers */
int chat_xml_build_presence(const char *from, const char *to, const char *show, const char *status, char *out_xml, int max_len) {
    ChatXmlNode *pres = chat_xml_node_alloc("presence");
    if (from) chat_xml_add_attr(pres, "from", from);
    if (to) chat_xml_add_attr(pres, "to", to);

    if (show) {
        ChatXmlNode *sh = chat_xml_node_alloc("show");
        strncpy(sh->cdata, show, sizeof(sh->cdata) - 1);
        chat_xml_add_child(pres, sh);
    }
    if (status) {
        ChatXmlNode *st = chat_xml_node_alloc("status");
        strncpy(st->cdata, status, sizeof(st->cdata) - 1);
        chat_xml_add_child(pres, st);
    }

    int res = chat_xml_format(pres, out_xml, max_len);
    chat_xml_free(pres);
    return res;
}

int chat_xml_build_message(const char *from, const char *to, const char *type, const char *body, const char *stamp, char *out_xml, int max_len) {
    ChatXmlNode *msg = chat_xml_node_alloc("message");
    if (from) chat_xml_add_attr(msg, "from", from);
    if (to) chat_xml_add_attr(msg, "to", to);
    if (type) chat_xml_add_attr(msg, "type", type);

    if (body) {
        ChatXmlNode *b = chat_xml_node_alloc("body");
        strncpy(b->cdata, body, sizeof(b->cdata) - 1);
        chat_xml_add_child(msg, b);
    }
    if (stamp) {
        ChatXmlNode *s = chat_xml_node_alloc("stamp");
        strncpy(s->cdata, stamp, sizeof(s->cdata) - 1);
        chat_xml_add_child(msg, s);
    }

    int res = chat_xml_format(msg, out_xml, max_len);
    chat_xml_free(msg);
    return res;
}

/* Fast Stanza Extraction Parser (Deterministic without memory leaks) */
static void extract_attr_val(const char *tag_str, const char *attr_name, char *out_val, int max_len) {
    out_val[0] = '\0';
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=\"", attr_name);
    char *p = strstr((char*)tag_str, needle);
    if (!p) {
        snprintf(needle, sizeof(needle), "%s='", attr_name);
        p = strstr((char*)tag_str, needle);
    }
    if (!p) return;
    p += strlen(needle);
    char *end = strpbrk(p, "\"'>");
    if (!end) return;
    int len = (int)(end - p);
    if (len >= max_len) len = max_len - 1;
    strncpy(out_val, p, len);
    out_val[len] = '\0';
}

static void extract_tag_cdata(const char *xml, const char *tag_name, char *out_cdata, int max_len) {
    out_cdata[0] = '\0';
    char open_tag[64];
    char close_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag_name);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag_name);

    char *start = strstr((char*)xml, open_tag);
    if (!start) return;
    start += strlen(open_tag);

    char *end = strstr(start, close_tag);
    if (!end) return;

    int len = (int)(end - start);
    if (len >= max_len) len = max_len - 1;
    strncpy(out_cdata, start, len);
    out_cdata[len] = '\0';
}

BOOL chat_xml_parse_message(const char *xml_str, char *out_from, char *out_to, char *out_type, char *out_body, char *out_stamp) {
    if (!xml_str || !strstr(xml_str, "<message")) return FALSE;

    if (out_from) extract_attr_val(xml_str, "from", out_from, CHAT_MAX_JID_LEN);
    if (out_to) extract_attr_val(xml_str, "to", out_to, CHAT_MAX_ROOM_LEN);
    if (out_type) extract_attr_val(xml_str, "type", out_type, 16);
    if (out_body) extract_tag_cdata(xml_str, "body", out_body, CHAT_MAX_LINE_LEN);
    if (out_stamp) extract_tag_cdata(xml_str, "stamp", out_stamp, 16);

    return TRUE;
}

BOOL chat_xml_parse_presence(const char *xml_str, char *out_from, char *out_to, char *out_show, char *out_status) {
    if (!xml_str || !strstr(xml_str, "<presence")) return FALSE;

    if (out_from) extract_attr_val(xml_str, "from", out_from, CHAT_MAX_JID_LEN);
    if (out_to) extract_attr_val(xml_str, "to", out_to, CHAT_MAX_ROOM_LEN);
    if (out_show) extract_tag_cdata(xml_str, "show", out_show, 16);
    if (out_status) extract_tag_cdata(xml_str, "status", out_status, 64);

    return TRUE;
}
