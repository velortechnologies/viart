#ifndef VIART_BROKER_INDEX_H
#define VIART_BROKER_INDEX_H
#include <stddef.h>
typedef struct bp_index_entry {char *pattern; void *owner; struct bp_index_entry *next;} bp_index_entry;
typedef struct {bp_index_entry *exact[256], *wildcards;} bp_index;
typedef void (*bp_index_visit_fn)(void *owner, void *context);
int bp_index_add(bp_index *idx, const char *pattern, void *owner);
void bp_index_remove(bp_index *idx, const char *pattern, void *owner);
void bp_index_drop_owner(bp_index *idx, void *owner);
void bp_index_visit(const bp_index *idx, const char *topic, bp_index_visit_fn fn, void *context);
void bp_index_clear(bp_index *idx);
#endif
