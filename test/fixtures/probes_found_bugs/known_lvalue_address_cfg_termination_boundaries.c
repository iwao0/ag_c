/*
 * Addresses of existing objects, subobjects, and constant-index elements are
 * nonnull and terminate an endless-loop CFG without evaluating object values.
 */

struct inner {
    int first;
    int second;
};

struct outer {
    struct inner nested;
};

union choice {
    int integer;
    double floating;
};

enum {
    ELEMENT_INDEX = 1
};

static struct inner global_object;
static struct outer global_outer;
static union choice global_choice;
static int global_values[2];
static int *global_pointer;

static int global_member_address_true(int value) {
    if (value == 42) return 42;
    while (&global_object.second) {
    }
}

static int nested_member_address_true(int value) {
    if (value == 42) return 42;
    while (&global_outer.nested.second) {
    }
}

static int union_member_address_true(int value) {
    if (value == 42) return 42;
    while (&global_choice.floating) {
    }
}

static int global_array_element_address_true(int value) {
    if (value == 42) return 42;
    while (&global_values[ELEMENT_INDEX]) {
    }
}

static int reversed_subscript_address_true(int value) {
    if (value == 42) return 42;
    while (&1[global_values]) {
    }
}

static int string_element_address_true(int value) {
    if (value == 42) return 42;
    while (&"abc"[1]) {
    }
}

static int known_pointer_member_address_true(int value) {
    if (value == 42) return 42;
    while (&(&global_object)->second) {
    }
}

static int pointer_object_address_true(int value) {
    if (value == 42) return 42;
    while (&global_pointer) {
    }
}

int main(void) {
    if (global_member_address_true(42) != 42) return 1;
    if (nested_member_address_true(42) != 42) return 2;
    if (union_member_address_true(42) != 42) return 3;
    if (global_array_element_address_true(42) != 42) return 4;
    if (reversed_subscript_address_true(42) != 42) return 5;
    if (string_element_address_true(42) != 42) return 6;
    if (known_pointer_member_address_true(42) != 42) return 7;
    if (pointer_object_address_true(42) != 42) return 8;

    if (!&global_object.second) return 9;
    if (!&global_outer.nested.second) return 10;
    if (!&global_choice.floating) return 11;
    if (!&global_values[ELEMENT_INDEX]) return 12;
    if (!&1[global_values]) return 13;
    if (!&"abc"[1]) return 14;
    if (!&(&global_object)->second) return 15;
    if (!&global_pointer) return 16;
    return 0;
}
