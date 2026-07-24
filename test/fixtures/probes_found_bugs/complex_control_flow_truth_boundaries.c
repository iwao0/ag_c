// Complex scalar truth in statements, loops, short-circuit, and ternaries.
// Expected: exit=0
#include <complex.h>

struct pair {
    int first;
    int second;
};

static double complex next_truth(int *count, int truth) {
    ++*count;
    return truth ? CMPLX(0.0, 1.0) : CMPLX(0.0, 0.0);
}

static int forbidden(int *count) {
    ++*count;
    return 1;
}

int main(void) {
    int condition_count = 0;
    int forbidden_count = 0;
    int score = 0;
    int while_index = 0;
    int do_index = 0;
    int for_index;
    int branch_value;
    int void_value = 0;
    struct pair aggregate_value;

    if (next_truth(&condition_count, 1))
        score += 1;
    else
        return 1;
    if (next_truth(&condition_count, 0))
        return 2;
    else
        score += 2;
    if (condition_count != 2 || score != 3)
        return 3;

    while (next_truth(&condition_count, while_index < 2)) {
        ++while_index;
        score += 4;
    }
    if (while_index != 2 || condition_count != 5 || score != 11)
        return 4;

    do {
        ++do_index;
        score += 8;
    } while (next_truth(&condition_count, do_index < 2));
    if (do_index != 2 || condition_count != 7 || score != 27)
        return 5;

    for (for_index = 0;
         next_truth(&condition_count, for_index < 3);
         ++for_index) {
        score += 16;
    }
    if (for_index != 3 || condition_count != 11 || score != 75)
        return 6;

    if (next_truth(&condition_count, 0) && forbidden(&forbidden_count))
        return 7;
    if (!(next_truth(&condition_count, 1) ||
          forbidden(&forbidden_count)))
        return 8;
    if (!(next_truth(&condition_count, 1) &&
          next_truth(&condition_count, 1)))
        return 9;
    if (next_truth(&condition_count, 0) ||
        next_truth(&condition_count, 0))
        return 10;
    if (forbidden_count != 0 || condition_count != 17)
        return 11;

    branch_value = next_truth(&condition_count, 0) ? 1 : 2;
    if (branch_value != 2)
        return 12;
    aggregate_value = next_truth(&condition_count, 0)
        ? (struct pair){3, 4}
        : (struct pair){5, 6};
    if (aggregate_value.first != 5 || aggregate_value.second != 6)
        return 13;
    next_truth(&condition_count, 1)
        ? (void)(void_value += 7)
        : (void)(void_value += 70);
    if (void_value != 7 || condition_count != 20)
        return 14;

    if (!CMPLXF(0.0f, -1.0f) || CMPLXF(0.0f, 0.0f))
        return 15;
    if (!CMPLXL(0.0L, -1.0L) || CMPLXL(0.0L, 0.0L))
        return 16;
    return 0;
}
