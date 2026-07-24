static int call_count;

static unsigned long long next_high_value(void) {
    call_count++;
    return 1ULL << 63;
}

int main(void) {
    unsigned long long unsigned_high = 1ULL << 63;
    long long signed_high = 1LL << 40;
    int loop_count = 0;

    if (!unsigned_high) return 1;
    if (!signed_high) return 2;

    if (unsigned_high ? 0 : 1) return 3;
    if (signed_high ? 0 : 1) return 4;

    while (unsigned_high) {
        loop_count++;
        unsigned_high = 0;
    }
    if (loop_count != 1) return 5;

    loop_count = 0;
    for (signed_high = -(1LL << 40); signed_high; signed_high = 0) {
        loop_count++;
    }
    if (loop_count != 1) return 6;

    call_count = 0;
    if (!next_high_value() || call_count != 1) return 7;

    call_count = 0;
    if (next_high_value()) {
        if (call_count != 1) return 8;
    } else {
        return 9;
    }

    return 0;
}
