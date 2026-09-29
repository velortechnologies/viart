#include "subscriptions.h"
#include <assert.h>
int main(void) {
    assert(bp_topic_match("#", "anything/nested"));
    assert(bp_topic_match("a/+", "a/b"));
    assert(!bp_topic_match("a/+", "a/b/c"));
    assert(bp_topic_match("a/#", "a/b/c"));
    assert(!bp_topic_match("a/b", "a/c"));
    assert(bp_name_match("group.?.*", "group.a.client"));
    assert(!bp_name_match("group.?.*", "group.a"));
    bp_sub *s = 0;
    assert(bp_sub_add(&s, "a/+") == 0);
    assert(bp_sub_add(&s, "a/+") == 0);
    assert(bp_sub_match(s, "a/b"));
    bp_sub_remove(&s, "a/+");
    assert(!bp_sub_match(s, "a/b"));
    bp_sub_free(s);
    return 0;
}
