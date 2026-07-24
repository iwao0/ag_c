/* A negative shift count cannot form an integer constant expression. */
int invalid_unsigned_long_long_bound[
    (1ULL << -1) ? 1 : -1];
