// stdlib.h nested callback API function pointer boundaries
// Expected: exit=0
#include <stddef.h>
#include <stdlib.h>

static int comparison_count;

static int compare_ints(const void *lhs, const void *rhs) {
    int left = *(const int *)lhs;
    int right = *(const int *)rhs;
    comparison_count++;
    return (left > right) - (left < right);
}

int main(void) {
    void (*sort_values)(void *, size_t, size_t,
                        int (*)(const void *, const void *)) = qsort;
    void *(*find_value)(const void *, const void *, size_t, size_t,
                        int (*)(const void *, const void *)) = bsearch;
    int values[6] = {5, -1, 3, 3, 0, 9};
    int key;
    int *found;
    int comparisons_after_sort;

    sort_values(values, 6, sizeof(values[0]), compare_ints);
    if (values[0] != -1 || values[1] != 0 || values[2] != 3 ||
        values[3] != 3 || values[4] != 5 || values[5] != 9) {
        return 1;
    }
    if (comparison_count == 0) return 2;

    comparisons_after_sort = comparison_count;
    sort_values(values, 0, sizeof(values[0]), compare_ints);
    if (comparison_count != comparisons_after_sort) return 3;

    key = 3;
    found = (int *)find_value(&key, values, 6, sizeof(values[0]), compare_ints);
    if (!found || *found != 3) return 4;

    key = 4;
    if (find_value(&key, values, 6, sizeof(values[0]), compare_ints) != 0) return 5;

    key = -1;
    if (find_value(&key, values, 0, sizeof(values[0]), compare_ints) != 0) return 6;
    return 0;
}
