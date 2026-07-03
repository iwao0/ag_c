// stdlib.h qsort/bsearch with struct records and a missing key.
// Expected: exit=0
#include <stdlib.h>
#include <assert.h>

struct Rec {
    int key;
    int value;
};

static int rec_cmp(void *ap, void *bp) {
    struct Rec *a = (struct Rec *)ap;
    struct Rec *b = (struct Rec *)bp;
    if (a->key != b->key) return (a->key > b->key) - (a->key < b->key);
    return (a->value > b->value) - (a->value < b->value);
}

int main(void) {
    struct Rec rows[5] = {
        {3, 30},
        {1, 10},
        {2, 21},
        {2, 20},
        {4, 40},
    };
    qsort(rows, 5, sizeof(rows[0]), rec_cmp);
    assert(rows[0].key == 1 && rows[0].value == 10);
    assert(rows[1].key == 2 && rows[1].value == 20);
    assert(rows[2].key == 2 && rows[2].value == 21);
    assert(rows[3].key == 3 && rows[3].value == 30);
    assert(rows[4].key == 4 && rows[4].value == 40);

    struct Rec key = {2, 21};
    struct Rec *found = (struct Rec *)bsearch(&key, rows, 5, sizeof(rows[0]), rec_cmp);
    assert(found != 0);
    assert(found->key == 2 && found->value == 21);

    struct Rec missing = {5, 0};
    assert(bsearch(&missing, rows, 5, sizeof(rows[0]), rec_cmp) == 0);
    return 0;
}
