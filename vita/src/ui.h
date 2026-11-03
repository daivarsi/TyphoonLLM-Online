/*
 * WoozyLLM Vita - chat-style UI.
 *
 * Draws a ChatGPT-inspired layout using only vita2d rectangles and PGF text.
 */

#ifndef WOOZYLLM_UI_H
#define WOOZYLLM_UI_H

#include <vita2d.h>

#define UI_MAX_MESSAGES 8
#define UI_MESSAGE_TEXT_MAX 1024

enum ui_role {
    UI_ROLE_USER = 0,
    UI_ROLE_ASSISTANT = 1
};

struct ui_message {
    enum ui_role role;
    char text[UI_MESSAGE_TEXT_MAX];
};

struct ui_chat {
    struct ui_message messages[UI_MAX_MESSAGES];
    int count;
    int ime_ready;
    int input_active;
    char status[160];
};

void ui_chat_init(struct ui_chat *chat);
void ui_chat_set_status(struct ui_chat *chat, const char *status);
void ui_chat_add_message(struct ui_chat *chat, enum ui_role role, const char *text);
void ui_chat_set_last_message(struct ui_chat *chat, enum ui_role role, const char *text);
void ui_chat_clear(struct ui_chat *chat);
void ui_draw_chat(vita2d_pgf *pgf, const struct ui_chat *chat);

#endif /* WOOZYLLM_UI_H */
