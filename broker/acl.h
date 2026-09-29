#ifndef VIART_BROKER_ACL_H
#define VIART_BROKER_ACL_H
#include <stdbool.h>
#include <stdint.h>
typedef struct bp_acl_rule bp_acl_rule;
typedef enum { BP_ACL_CONNECT, BP_ACL_SUBSCRIBE, BP_ACL_PUBLISH,
               BP_ACL_P2P, BP_ACL_BROADCAST } bp_acl_action;
/* If no policy file is supplied, the broker retains its permissive legacy mode.
 * Once supplied, policy is allow-list only and unmatched requests are denied. */
int bp_acl_load(const char *path, bp_acl_rule **out);
void bp_acl_free(bp_acl_rule *rules);
bool bp_acl_allowed(const bp_acl_rule *rules, const char *identity,
                    bp_acl_action action, const char *target, uint32_t ipv4_host_order,
                    bool local, uint32_t uid);
#endif
