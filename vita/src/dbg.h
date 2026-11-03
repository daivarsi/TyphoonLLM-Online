/*
 * WoozyLLM Vita - шаг 2: диагностика.
 *
 * Минимальный логгер в ux0:data/woozyllm.log.
 */

#ifndef WOOZYLLM_DBG_H
#define WOOZYLLM_DBG_H

void dbg_init(void);
void dbg_log(const char *fmt, ...);

#endif /* WOOZYLLM_DBG_H */
