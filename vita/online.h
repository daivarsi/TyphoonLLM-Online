#pragma once
#include <stdint.h>
typedef struct {char title[129],source[385],evidence[1025];uint32_t raw_bytes,elapsed_ms;uint64_t timestamp;} RetrievalResult;
enum { ONLINE_OFFLINE=0, ONLINE_AUTO=1, ONLINE_FORCE=2 };
int online_mode(void);
void online_load_settings(void);
void online_set_mode(int value);
void online_cycle_mode(void);
const char *online_mode_name(void);
const char *online_status(void);
int online_should_retrieve(const char *query);
void online_fallback(void);
void online_discard_reference(void);
int retrieval_query(const char *query,RetrievalResult *out,volatile int *cancelled);
void online_generating(int used);
void online_complete(int used);
void online_failure(int insufficient);

void online_start(void);
void online_planning(void);
void online_reset_context(void);
void online_remember_topic(const char *topic);
const char *online_context_topic(void);
void online_remember_facts(const char *facts);
const char *online_reference_notes(void);

enum { ONLINE_BADGE_OFFLINE, ONLINE_BADGE_ONLINE, ONLINE_BADGE_CHECKING, ONLINE_BADGE_AUTO };
int online_badge_state(void);
const char*online_badge_label(int badge);
