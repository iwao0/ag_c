// 外部リンケージの暫定定義は、後続のtag定義によって完全型になれる。
#include <assert.h>

struct DeferredPair;
struct DeferredPair deferred_pair;
extern struct DeferredPair deferred_pair;

struct DeferredPair {
    int left;
    int right;
};

int main(void) {
    assert(deferred_pair.left == 0);
    assert(deferred_pair.right == 0);
    deferred_pair.left = 19;
    deferred_pair.right = 23;
    assert(deferred_pair.left + deferred_pair.right == 42);
    return 0;
}
