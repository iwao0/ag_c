/* Atomic cannot qualify a tag-only declaration. */
_Atomic enum { KIND_ZERO };

int main(void) { return KIND_ZERO; }
