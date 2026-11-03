/*
 * WoozyLLM Vita - UI data model, decoupled from any backend.
 *
 * The render loop only reads these structs. A future network backend
 * (OpenAI API via curl, etc.) can fill / update them without touching UI code.
 */

#ifndef WOOZYLLM_UI_MODEL_H
#define WOOZYLLM_UI_MODEL_H

#include <string>
#include <vector>

struct ChatMessage {
    std::string text;
    bool        is_user;   // true  = right-aligned orange bubble (User)
                           // false = left-aligned gray bubble  (Assistant)
    ChatMessage() : is_user(false) {}
    ChatMessage(const std::string &t, bool u) : text(t), is_user(u) {}
};

struct ChatSession {
    std::string title;                 // snippet of the first user message
    std::vector<ChatMessage> messages; // ordered conversation history
    bool generating = false;           // backend is producing an answer right now
    std::string file;                  // recents file name once persisted ("")
};

struct AppState {
    std::vector<ChatSession> sessions; // chat history (sidebar)
    size_t current = 0;                // index of the active session

    std::string draft;                 // text typed via the on-screen IME, awaiting Send
    std::string status;                // thin status line at the bottom
    bool sidebar_open = false;
    bool show_settings = false;          // fullscreen settings overlay

    // one-shot UI->app signals, consumed (and cleared) by main.c
    bool want_input = false;           // user tapped the input field -> open IME
    bool want_send = false;            // user pressed Send with non-empty draft
    bool want_stop = false;            // user pressed Stop while AI is generating
};

#endif /* WOOZYLLM_UI_MODEL_H */