/*
 * WoozyLLM Vita - шаг 2.
 *
 * Неблокирующая обёртка над системной клавиатурой PS Vita (sceImeDialog).
 */

#ifndef WOOZYLLM_IME_H
#define WOOZYLLM_IME_H

enum ime_status {
    IME_STATUS_NONE = 0,
    IME_STATUS_RUNNING,
    IME_STATUS_FINISHED
};

int ime_init(void);
int ime_open(const char *title, const char *initial);
int ime_get_status(void);
const char *ime_get_text(void);
void ime_term(void);

#endif /* WOOZYLLM_IME_H */
