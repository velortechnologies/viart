#define _POSIX_C_SOURCE 200809L
#include "subscriptions.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

bool bp_topic_match(const char *pattern, const char *topic) {
    while (*pattern || *topic) {
        const char *p_end = strchr(pattern, '/');
        const char *t_end = strchr(topic, '/');
        size_t pn = p_end ? (size_t)(p_end - pattern) : strlen(pattern);
        size_t tn = t_end ? (size_t)(t_end - topic) : strlen(topic);
        if (pn == 1 && pattern[0] == '#' && !p_end) return true;
        if (!(pn == 1 && pattern[0] == '+') && (pn != tn || memcmp(pattern, topic, pn))) return false;
        if (!p_end || !t_end) return !p_end && !t_end;
        pattern = p_end + 1;
        topic = t_end + 1;
    }
    return true;
}

int bp_sub_add(bp_sub **head, const char *pattern) {
    if (!head || !pattern || !*pattern) return EINVAL;
    for (bp_sub *s = *head; s; s = s->next) if (!strcmp(s->pattern, pattern)) return 0;
    bp_sub *s = malloc(sizeof(*s));
    if (!s) return ENOMEM;
    s->pattern = strdup(pattern);
    if (!s->pattern) { free(s); return ENOMEM; }
    s->next = *head;
    *head = s;
    return 0;
}
void bp_sub_remove(bp_sub **head, const char *pattern) {
    if (!head || !pattern) return;
    while (*head) {
        if (!strcmp((*head)->pattern, pattern)) {
            bp_sub *old = *head;
            *head = old->next;
            free(old->pattern); free(old);
            return;
        }
        head = &(*head)->next;
    }
}
void bp_sub_free(bp_sub *head) {
    while (head) { bp_sub *next = head->next; free(head->pattern); free(head); head = next; }
}
bool bp_sub_match(const bp_sub *head, const char *topic) {
    for (const bp_sub *s = head; s; s = s->next)
        if (bp_topic_match(s->pattern, topic)) return true;
    return false;
}

bool bp_name_match(const char *mask, const char *name) {
    while (*mask || *name) {
        const char *m_end = strchr(mask, '.');
        const char *n_end = strchr(name, '.');
        size_t mn = m_end ? (size_t)(m_end - mask) : strlen(mask);
        size_t nn = n_end ? (size_t)(n_end - name) : strlen(name);
        if (mn == 1 && mask[0] == '*' && !m_end) return true;
        if (!(mn == 1 && mask[0] == '?') && (mn != nn || memcmp(mask, name, mn))) return false;
        if (!m_end || !n_end) return !m_end && !n_end;
        mask = m_end + 1;
        name = n_end + 1;
    }
    return true;
}
