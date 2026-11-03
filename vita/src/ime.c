/*
 * WoozyLLM Vita - шаг 2.
 *
 * Реализация sceImeDialog. Диалог работает асинхронно:
 * sceImeDialogInit() открывает клавиатуру, sceImeDialogGetStatus() опрашивается
 * каждый кадр, а результат читается через sceImeDialogGetResult().
 */

#include <stdio.h>
#include <string.h>

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>

#include "dbg.h"
#include "ime.h"

#define IME_MAX_TEXT_LEN 512

static SceWChar16 s_input[IME_MAX_TEXT_LEN + 1];
static SceWChar16 s_title[SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1];
static SceWChar16 s_initial[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static char s_text_utf8[IME_MAX_TEXT_LEN * 4 + 1];
static int s_status = IME_STATUS_NONE;

static size_t utf8_to_utf16(const char *src, SceWChar16 *dst, size_t dst_cap)
{
    size_t out = 0;

    while (*src && out + 1 < dst_cap) {
        unsigned int cp;
        unsigned char c = (unsigned char)*src++;

        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && src[0]) {
            cp = ((unsigned int)(c & 0x1F) << 6)
               | ((unsigned int)(*src++ & 0x3F));
        } else if ((c & 0xF0) == 0xE0 && src[0] && src[1]) {
            cp = ((unsigned int)(c & 0x0F) << 12)
               | ((unsigned int)(src[0] & 0x3F) << 6)
               | ((unsigned int)(src[1] & 0x3F));
            src += 2;
        } else if ((c & 0xF8) == 0xF0 && src[0] && src[1] && src[2]) {
            cp = ((unsigned int)(c & 0x07) << 18)
               | ((unsigned int)(src[0] & 0x3F) << 12)
               | ((unsigned int)(src[1] & 0x3F) << 6)
               | ((unsigned int)(src[2] & 0x3F));
            src += 3;
        } else {
            cp = 0xFFFD;
        }

        if (cp > 0xFFFF) {
            if (out + 2 >= dst_cap)
                break;
            cp -= 0x10000;
            dst[out++] = (SceWChar16)(0xD800 | (cp >> 10));
            dst[out++] = (SceWChar16)(0xDC00 | (cp & 0x3FF));
        } else {
            dst[out++] = (SceWChar16)cp;
        }
    }

    dst[out] = 0;
    return out;
}

static size_t utf16_to_utf8(const SceWChar16 *src, char *dst, size_t dst_cap)
{
    size_t out = 0;

    while (*src && out + 1 < dst_cap) {
        unsigned int cp = *src++;

        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (*src >= 0xDC00 && *src <= 0xDFFF)
                cp = 0x10000 + ((cp - 0xD800) << 10) + (*src++ - 0xDC00);
            else
                cp = 0xFFFD;
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }

        if (cp < 0x80) {
            dst[out++] = (char)cp;
        } else if (cp < 0x800) {
            if (out + 2 >= dst_cap)
                break;
            dst[out++] = (char)(0xC0 | (cp >> 6));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            if (out + 3 >= dst_cap)
                break;
            dst[out++] = (char)(0xE0 | (cp >> 12));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (out + 4 >= dst_cap)
                break;
            dst[out++] = (char)(0xF0 | (cp >> 18));
            dst[out++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    dst[out] = 0;
    return out;
}

int ime_init(void)
{
    SceAppUtilInitParam app_util_param;
    SceAppUtilBootParam boot_param;
    int ret;

    memset(&app_util_param, 0, sizeof(app_util_param));
    memset(&boot_param, 0, sizeof(boot_param));

    ret = sceAppUtilInit(&app_util_param, &boot_param);
    dbg_log("ime_init: sceAppUtilInit ret=0x%08X", (unsigned int)ret);
    if (ret < 0)
        return ret;

    ret = sceCommonDialogSetConfigParam(&(SceCommonDialogConfigParam){});
    dbg_log("ime_init: sceCommonDialogSetConfigParam ret=0x%08X", (unsigned int)ret);
    if (ret < 0)
        return ret;

    s_status = IME_STATUS_NONE;
    return 0;
}

int ime_open(const char *title, const char *initial)
{
    SceImeDialogParam param;
    int ret;

    if (s_status == IME_STATUS_RUNNING)
        return 0;

    utf8_to_utf16(title ? title : "", s_title, SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1);
    utf8_to_utf16(initial ? initial : "", s_initial, SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1);
    memset(s_input, 0, sizeof(s_input));
    s_text_utf8[0] = 0;

    ret = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    dbg_log("ime_open: sceSysmoduleLoadModule ret=0x%08X", (unsigned int)ret);
    if (ret < 0)
        return ret;

    sceImeDialogParamInit(&param);
    param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH | SCE_IME_LANGUAGE_RUSSIAN;
    param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.option = 0;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_DEFAULT;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = s_title;
    param.maxTextLength = IME_MAX_TEXT_LEN;
    param.initialText = s_initial;
    param.inputTextBuffer = s_input;

    ret = sceImeDialogInit(&param);
    dbg_log("ime_open: sceImeDialogInit ret=0x%08X", (unsigned int)ret);
    if (ret < 0)
        return ret;

    s_status = IME_STATUS_RUNNING;
    return 0;
}

int ime_get_status(void)
{
    if (s_status == IME_STATUS_RUNNING) {
        SceCommonDialogStatus status = sceImeDialogGetStatus();

        if (status == SCE_COMMON_DIALOG_STATUS_RUNNING) {
            /* LIVE SYNC: the system writes into s_input (inputTextBuffer) as
             * the user types. Convert it every frame so ime_get_text() always
             * returns the current text and the UI preview updates in real
             * time - no need to wait for Enter. */
            utf16_to_utf8(s_input, s_text_utf8, sizeof(s_text_utf8));
        } else if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) {
            SceImeDialogResult result;
            memset(&result, 0, sizeof(result));

            if (sceImeDialogGetResult(&result) >= 0 &&
                result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
                utf16_to_utf8(s_input, s_text_utf8, sizeof(s_text_utf8));
            } else {
                s_text_utf8[0] = 0;
            }

            sceImeDialogTerm();
            s_status = IME_STATUS_FINISHED;
        } else if (status == SCE_COMMON_DIALOG_STATUS_NONE) {
            s_text_utf8[0] = 0;
            s_status = IME_STATUS_FINISHED;
        }
    }

    return s_status;
}

const char *ime_get_text(void)
{
    return s_text_utf8;
}

void ime_term(void)
{
    s_status = IME_STATUS_NONE;
}
