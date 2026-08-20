/* Enumerator declarations do not satisfy the aggregate named-member rule. */
struct Item {
  enum { ITEM_READY = 1 };
};
