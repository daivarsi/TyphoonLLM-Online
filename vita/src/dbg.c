/*
 * WoozyLLM Vita - шаг 2: диагностика.
 *
 * Логгер открывает файл один раз и сбрасывает буфер после каждой строки,
 * чтобы коды ошибок Vita API сохранялись даже при падении приложения.
 */

#include <stdarg.h>
#include <stdio.h>

#include <psp2/kernel/processmgr.h>

#include "dbg.h"

static FILE *s_log;

/* monotonic process time in ms (1us ticks from the kernel, scaled down) */
static long dbg_now_ms(void)
{
    return (long)(sceKernelGetProcessTimeWide() / 1000);
}

void dbg_init(void)
{
    s_log = fopen("ux0:data/VitaAIResearch/woozy-correctness.log", "w");
    if (s_log) {
        fprintf(s_log, "=== WoozyLLM debug log ===\n");
        fflush(s_log);
    }
}

void dbg_log(const char *fmt, ...)
{
    va_list ap;

    if (!s_log)
        return;

    fprintf(s_log, "[%ld ms] ", dbg_now_ms());
    va_start(ap, fmt);
    vfprintf(s_log, fmt, ap);
    va_end(ap);

    fprintf(s_log, "\n");
    fflush(s_log);
}
