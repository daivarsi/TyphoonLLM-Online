/*
 * WoozyLLM Vita - Dear ImGui UI bridge (C interface for main.c).
 *
 * main.c keeps owning the app loop (IME, worker threads). The UI layer exposes
 * a small set of C functions to inject data (messages, status) and to consume
 * one-shot intent flags (open keyboard / send). The backend stays out of the
 * render loop entirely.
 */

#ifndef WOOZYLLM_IMGUI_UI_H
#define WOOZYLLM_IMGUI_UI_H

#ifdef __cplusplus
extern "C" {
#endif

int  imgui_ui_init(void);
void imgui_ui_begin(void);
void imgui_ui_draw(void);
void imgui_ui_end(void);
void imgui_ui_fini(void);

/* --- data injection (from main.c) --- */
void imgui_ui_set_status(const char *status);
void imgui_ui_set_draft(const char *text);            /* text returned by the IME */
const char *imgui_ui_take_draft(void);                 /* get draft and clear it */
int  imgui_ui_send_message(const char *text);          /* user bubble + assistant
                                                          placeholder in one step */
void imgui_ui_stream_assistant(const char *text);      /* fill the active placeholder */
void imgui_ui_end_assistant(void);                     /* stop streaming */
void imgui_ui_new_chat(void);                          /* "New Chat" action */

/* --- one-shot flags, consumed (cleared) from main.c --- */
int  imgui_ui_want_input(void);
void imgui_ui_clear_want_input(void);
int  imgui_ui_want_send(void);
void imgui_ui_clear_want_send(void);
int  imgui_ui_want_stop(void);      /* Stop pressed while AI is generating */
void imgui_ui_clear_want_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* WOOZYLLM_IMGUI_UI_H */