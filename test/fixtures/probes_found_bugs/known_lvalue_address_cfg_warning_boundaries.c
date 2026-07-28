/*
 * Runtime indexing, unknown pointer bases, and compound-literal storage keep
 * Clang's conservative fallthrough warning and must retain W3005 in ag_c.
 */

struct pair {
    int first;
    int second;
};

static struct pair object;
static int values[2];
static int side_effect_count;

static int next_value(int value) {
    side_effect_count += 1;
    return value;
}

static int runtime_array_index_address_unknown(void) {
    while (&values[next_value(0)]) {
    }
}

static int runtime_string_index_address_unknown(void) {
    while (&"abc"[next_value(0)]) {
    }
}

static int pointer_member_address_unknown(struct pair *pointer) {
    while (&pointer->second) {
    }
}

static int scalar_compound_address_unknown(void) {
    while (&(int){next_value(1)}) {
    }
}

static int aggregate_compound_member_address_unknown(void) {
    while (&((struct pair){next_value(1), 2}).second) {
    }
}

int main(void) {
    if (!&values[next_value(0)]) return 1;
    if (side_effect_count != 1) return 2;
    if (!&"abc"[next_value(0)]) return 3;
    if (side_effect_count != 2) return 4;
    if (!&(&object)->second) return 5;
    if (!&(int){next_value(1)}) return 6;
    if (side_effect_count != 3) return 7;
    if (!&((struct pair){next_value(1), 2}).second) return 8;
    if (side_effect_count != 4) return 9;
    return 0;
}
