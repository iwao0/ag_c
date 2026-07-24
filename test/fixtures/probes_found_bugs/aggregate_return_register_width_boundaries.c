// Aggregate return register widths, including partial final registers.
// Expected: exit=0

#define DEFINE_AGGREGATE_CASE(N)                                           \
    typedef struct {                                                       \
        unsigned char bytes[N];                                            \
    } aggregate_##N##_t;                                                   \
                                                                           \
    static aggregate_##N##_t make_##N(unsigned char seed) {                \
        aggregate_##N##_t value = {{0}};                                   \
        int i;                                                             \
        for (i = 0; i < N; i++)                                            \
            value.bytes[i] = (unsigned char)(seed + i);                    \
        return value;                                                      \
    }                                                                      \
                                                                           \
    static int check_##N(void) {                                           \
        struct {                                                           \
            unsigned char before;                                          \
            aggregate_##N##_t value;                                       \
            unsigned char after;                                           \
        } box;                                                             \
        aggregate_##N##_t (*function)(unsigned char) = make_##N;           \
        int i;                                                             \
        box.before = 0x5a;                                                 \
        box.after = 0xa5;                                                  \
        box.value = function((unsigned char)N);                            \
        if (box.before != 0x5a || box.after != 0xa5) return 1;             \
        for (i = 0; i < N; i++) {                                          \
            if (box.value.bytes[i] != (unsigned char)(N + i)) return 1;    \
        }                                                                  \
        return 0;                                                          \
    }

DEFINE_AGGREGATE_CASE(3)
DEFINE_AGGREGATE_CASE(5)
DEFINE_AGGREGATE_CASE(6)
DEFINE_AGGREGATE_CASE(7)
DEFINE_AGGREGATE_CASE(9)
DEFINE_AGGREGATE_CASE(10)
DEFINE_AGGREGATE_CASE(11)
DEFINE_AGGREGATE_CASE(12)
DEFINE_AGGREGATE_CASE(13)
DEFINE_AGGREGATE_CASE(14)
DEFINE_AGGREGATE_CASE(15)
DEFINE_AGGREGATE_CASE(16)

int main(void) {
    if (check_3()) return 3;
    if (check_5()) return 5;
    if (check_6()) return 6;
    if (check_7()) return 7;
    if (check_9()) return 9;
    if (check_10()) return 10;
    if (check_11()) return 11;
    if (check_12()) return 12;
    if (check_13()) return 13;
    if (check_14()) return 14;
    if (check_15()) return 15;
    if (check_16()) return 16;
    return 0;
}
