/*
 * WoozyLLM Vita - chat-style UI.
 *
 * The PS Vita screen is 960x544. This UI keeps the header, chat history and
 * input hint visible without allocating dynamic memory.
 */

#include <stdio.h>
#include <string.h>

#include "ui.h"

#define SCREEN_W 960
#define SCREEN_H 544

#define COLOR_BG RGBA8(13, 17, 23, 255)
#define COLOR_HEADER RGBA8(32, 38, 48, 255)
#define COLOR_PANEL RGBA8(22, 27, 34, 255)
#define COLOR_USER RGBA8(16, 163, 127, 255)
#define COLOR_ASSISTANT RGBA8(48, 54, 64, 255)
#define COLOR_INPUT RGBA8(30, 36, 45, 255)
#define COLOR_WHITE RGBA8(245, 245, 245, 255)
#define COLOR_MUTED RGBA8(160, 166, 176, 255)
#define COLOR_ERROR RGBA8(255, 110, 110, 255)
#define COLOR_ACCENT RGBA8(110, 220, 255, 255)

static int text_width(vita2d_pgf *pgf, const char *text, float scale)
{
    return vita2d_pgf_text_width(pgf, scale, text);
}

static int text_height(vita2d_pgf *pgf, const char *text, float scale)
{
    int height = vita2d_pgf_text_height(pgf, scale, text);
    return height > 0 ? height : 20;
}

static int count_wrapped_lines(vita2d_pgf *pgf, const char *text, int max_w, float scale)
{
    char line[UI_MESSAGE_TEXT_MAX];
    char candidate[UI_MESSAGE_TEXT_MAX];
    const char *cursor = text;
    int lines = 0;
    size_t line_len = 0;

    line[0] = 0;

    while (*cursor) {
        char word[256];
        size_t word_len = 0;

        while (*cursor == ' ')
            cursor++;

        if (*cursor == '\n') {
            lines++;
            line[0] = 0;
            line_len = 0;
            cursor++;
            continue;
        }

        while (*cursor && *cursor != ' ' && *cursor != '\n' && word_len + 1 < sizeof(word))
            word[word_len++] = *cursor++;
        word[word_len] = 0;

        if (word_len == 0)
            continue;

        if (line_len == 0)
            snprintf(candidate, sizeof(candidate), "%s", word);
        else
            snprintf(candidate, sizeof(candidate), "%s %s", line, word);

        if (line_len > 0 && text_width(pgf, candidate, scale) > max_w) {
            lines++;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }

        line_len = strlen(line);
    }

    if (line_len > 0 || lines == 0)
        lines++;

    return lines;
}

static int draw_wrapped_text(vita2d_pgf *pgf, const char *text, int x, int y,
                             int max_w, unsigned int color, float scale)
{
    char line[UI_MESSAGE_TEXT_MAX];
    char candidate[UI_MESSAGE_TEXT_MAX];
    const char *cursor = text;
    size_t line_len = 0;
    int yy = y;
    int line_h = text_height(pgf, "Ag", scale) + 6;

    line[0] = 0;

    while (*cursor) {
        char word[256];
        size_t word_len = 0;

        while (*cursor == ' ')
            cursor++;

        if (*cursor == '\n') {
            if (line_len > 0) {
                vita2d_pgf_draw_text(pgf, x, yy, color, scale, line);
                yy += line_h;
                line[0] = 0;
                line_len = 0;
            }
            cursor++;
            continue;
        }

        while (*cursor && *cursor != ' ' && *cursor != '\n' && word_len + 1 < sizeof(word))
            word[word_len++] = *cursor++;
        word[word_len] = 0;

        if (word_len == 0)
            continue;

        if (line_len == 0)
            snprintf(candidate, sizeof(candidate), "%s", word);
        else
            snprintf(candidate, sizeof(candidate), "%s %s", line, word);

        if (line_len > 0 && text_width(pgf, candidate, scale) > max_w) {
            vita2d_pgf_draw_text(pgf, x, yy, color, scale, line);
            yy += line_h;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }

        line_len = strlen(line);
    }

    if (line_len > 0)
        vita2d_pgf_draw_text(pgf, x, yy, color, scale, line);

    return yy + line_h;
}

static int bubble_height(vita2d_pgf *pgf, const char *text, int text_w)
{
    int line_h = text_height(pgf, "Ag", 0.85f) + 6;
    return 34 + count_wrapped_lines(pgf, text, text_w, 0.85f) * line_h;
}

static int draw_bubble(vita2d_pgf *pgf, const struct ui_message *message, int y)
{
    const int margin = 28;
    const int bubble_w = 640;
    const int text_pad = 18;
    const int text_w = bubble_w - text_pad * 2;
    const int user_x = SCREEN_W - margin - bubble_w;
    const int assistant_x = margin;
    const int is_user = message->role == UI_ROLE_USER;
    const int x = is_user ? user_x : assistant_x;
    const int h = bubble_height(pgf, message->text, text_w);
    const unsigned int bg = is_user ? COLOR_USER : COLOR_ASSISTANT;
    const char *label = is_user ? "You" : "WoozyLLM";

    vita2d_draw_rectangle(x, y, bubble_w, h, bg);
    vita2d_pgf_draw_text(pgf, x + text_pad, y + 24, COLOR_WHITE, 0.78f, label);
    draw_wrapped_text(pgf, message->text, x + text_pad, y + 52, text_w, COLOR_WHITE, 0.85f);

    return y + h + 14;
}

void ui_chat_init(struct ui_chat *chat)
{
    memset(chat, 0, sizeof(*chat));
    chat->ime_ready = 1;
    snprintf(chat->status, sizeof(chat->status), "Cross: chat    Triangle: clear    START: exit");
}

void ui_chat_set_status(struct ui_chat *chat, const char *status)
{
    snprintf(chat->status, sizeof(chat->status), "%s", status ? status : "");
}

void ui_chat_add_message(struct ui_chat *chat, enum ui_role role, const char *text)
{
    int index;

    if (!text || !*text)
        return;

    if (chat->count == UI_MAX_MESSAGES) {
        memmove(&chat->messages[0], &chat->messages[1],
                sizeof(chat->messages[0]) * (UI_MAX_MESSAGES - 1));
        chat->count--;
    }

    index = chat->count++;
    chat->messages[index].role = role;
    snprintf(chat->messages[index].text, sizeof(chat->messages[index].text), "%s", text);
}

void ui_chat_set_last_message(struct ui_chat *chat, enum ui_role role, const char *text)
{
    if (chat->count <= 0 || chat->messages[chat->count - 1].role != role) {
        ui_chat_add_message(chat, role, text);
        return;
    }

    snprintf(chat->messages[chat->count - 1].text,
             sizeof(chat->messages[chat->count - 1].text), "%s", text ? text : "");
}

void ui_chat_clear(struct ui_chat *chat)
{
    chat->count = 0;
}

void ui_draw_chat(vita2d_pgf *pgf, const struct ui_chat *chat)
{
    int y = 78;
    int first = 0;
    int i;

    vita2d_draw_rectangle(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
    vita2d_draw_rectangle(0, 0, SCREEN_W, 62, COLOR_HEADER);
    vita2d_draw_rectangle(0, 62, SCREEN_W, 1, RGBA8(54, 62, 74, 255));

    vita2d_pgf_draw_text(pgf, 24, 39, COLOR_WHITE, 1.05f, "WoozyLLM");
    vita2d_pgf_draw_text(pgf, 190, 39, COLOR_MUTED, 0.82f, "local llama2.c assistant");

    if (!chat->ime_ready) {
        vita2d_pgf_draw_text(pgf, 24, 94, COLOR_ERROR, 1.0f, chat->status);
    } else if (chat->count == 0) {
        vita2d_draw_rectangle(150, 140, 660, 170, COLOR_PANEL);
        vita2d_pgf_draw_text(pgf, 244, 196, COLOR_WHITE, 1.15f,
                             "WoozyLLM - Press Cross to Chat");
        vita2d_pgf_draw_text(pgf, 270, 236, COLOR_MUTED, 0.85f,
                             "Type a prompt with the PS Vita keyboard.");
    } else {
        int visible_h[UI_MAX_MESSAGES];
        int total_h = 0;

        for (i = 0; i < chat->count; i++) {
            visible_h[i] = bubble_height(pgf, chat->messages[i].text, 604) + 14;
            total_h += visible_h[i];
        }

        while (first < chat->count && total_h > 362) {
            total_h -= visible_h[first];
            first++;
        }

        for (i = first; i < chat->count; i++)
            y = draw_bubble(pgf, &chat->messages[i], y);
    }

    vita2d_draw_rectangle(0, 462, SCREEN_W, 82, COLOR_HEADER);
    vita2d_draw_rectangle(26, 478, 908, 44, COLOR_INPUT);

    if (chat->input_active)
        vita2d_pgf_draw_text(pgf, 48, 507, COLOR_ACCENT, 0.86f, "Keyboard is open...");
    else
        vita2d_pgf_draw_text(pgf, 48, 507, COLOR_MUTED, 0.86f, chat->status);
}
