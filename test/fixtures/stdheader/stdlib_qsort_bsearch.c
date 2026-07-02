// stdlib.h の qsort/bsearch
// 期待: exit=0
#include <stdlib.h>
#include <assert.h>

static int int_cmp(void *ap, void *bp) {
    int a = *(int *)ap;
    int b = *(int *)bp;
    return (a > b) - (a < b);
}

int main(void) {
    int nums[5] = {4, 1, 5, 2, 3};
    int key = 3;
    qsort(nums, 5, sizeof(int), int_cmp);
    assert(nums[0] == 1);
    assert(nums[1] == 2);
    assert(nums[2] == 3);
    assert(nums[3] == 4);
    assert(nums[4] == 5);
    int *found = bsearch(&key, nums, 5, sizeof(int), int_cmp);
    assert(found != 0);
    assert(*found == 3);
    return 0;
}
