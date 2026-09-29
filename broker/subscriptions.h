#ifndef VIART_BROKER_SUBSCRIPTIONS_H
#define VIART_BROKER_SUBSCRIPTIONS_H
#include <stdbool.h>
#include <stddef.h>

typedef struct bp_sub { char *pattern; struct bp_sub *next; } bp_sub;
int bp_sub_add(bp_sub **head, const char *pattern);
void bp_sub_remove(bp_sub **head, const char *pattern);
void bp_sub_free(bp_sub *head);
bool bp_sub_match(const bp_sub *head, const char *topic);
bool bp_topic_match(const char *pattern, const char *topic);
bool bp_name_match(const char *mask, const char *name);
#endif
