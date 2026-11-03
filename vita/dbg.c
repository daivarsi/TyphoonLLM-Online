/*
 * WoozyLLM Vita - шаг 2: диагностика.
 *
 * Логгер открывает файл один раз и сбрасывает буфер после каждой строки,
 * чтобы коды ошибок Vita API сохранялись даже при падении приложения.
 */

#include <stdarg.h>
#include <stdio.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr/mutex.h>

#include "dbg.h"
#include "app_paths.h"

static FILE *s_log;
static SceUID s_log_mutex = -1;

/* monotonic process time in ms (1us ticks from the kernel, scaled down) */
static long dbg_now_ms(void)
{
    return (long)(sceKernelGetProcessTimeWide() / 1000);
}

void dbg_init(void)
{
    /* Called once by main before starting any worker. Keep this mutex for the
     * process lifetime: shutdown may still have workers writing diagnostics. */
    if (s_log_mutex >= 0)
        return;
    s_log_mutex = sceKernelCreateMutex("VitaLM debug log", 0, 0, NULL);
    if (s_log_mutex < 0)
        return;
    sceIoMkdir(app_data_dir(), 0777);
    s_log = fopen(app_log_path(), "w");
    if (s_log) {
        fprintf(s_log, "=== TyphoonLLM Online 00.20 / VLMNET001 session log ===\n");
        fflush(s_log);
    }
}

void dbg_log(const char *fmt, ...)
{
    va_list ap;

    if (!s_log || s_log_mutex < 0)
        return;

    /* Protect the entire record, including timestamp and flush. Individual
     * stdio calls may be thread safe without making a multi-call record so.
     * Never fall back to an unlocked write if acquiring the mutex fails. */
    if (sceKernelLockMutex(s_log_mutex, 1, NULL) < 0)
        return;

    fprintf(s_log, "[%ld ms] ", dbg_now_ms());
    va_start(ap, fmt);
    vfprintf(s_log, fmt, ap);
    va_end(ap);

    fprintf(s_log, "\n");
    fflush(s_log);
    sceKernelUnlockMutex(s_log_mutex, 1);
}
