#ifndef VIART_BROKER_AUTH_H
#define VIART_BROKER_AUTH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct bp_auth_entry bp_auth_entry;
int bp_auth_load(const char *path,bp_auth_entry **out);
void bp_auth_free(bp_auth_entry *entries);
bool bp_auth_check(const bp_auth_entry *entries,const char *name,const uint8_t digest[32]);
void bp_auth_digest(const char *token,size_t len,uint8_t digest[32]);
#endif
