/* A block-scope function may hide a file-scope typedef or enum constant.
 * Its external-linkage backing symbol must not replace the file declaration
 * outside the block, and the function name is not in scope in its own
 * parameter list (C11 6.2.1p7, 6.2.2p4-5). */
typedef int DirectType;
typedef int NamedType;
typedef int CommentType;
typedef int SpliceType;
typedef int PointerReturnType;
typedef int ExternType;
typedef int ParenthesizedType;
typedef int RepeatedType;
enum { EnumShadow = 4 };

int main(void) {
  int total = 0;
  {
    int DirectType(DirectType);
    total += sizeof(&DirectType) == sizeof(void *);
  }
  {
    int NamedType(NamedType value);
    total += sizeof(&NamedType) == sizeof(void *);
  }
  {
    int CommentType(/* parameter */ CommentType);
    total += sizeof(&CommentType) == sizeof(void *);
  }
  {
    int SpliceType(\
SpliceType);
    total += sizeof(&SpliceType) == sizeof(void *);
  }
  {
    int *PointerReturnType(PointerReturnType);
    total += sizeof(&PointerReturnType) == sizeof(void *);
  }
  {
    extern int ExternType(ExternType);
    total += sizeof(&ExternType) == sizeof(void *);
  }
  {
    int (ParenthesizedType)(ParenthesizedType);
    total += sizeof(&ParenthesizedType) == sizeof(void *);
  }
  {
    int RepeatedType(RepeatedType);
    total += sizeof(&RepeatedType) == sizeof(void *);
  }
  {
    int RepeatedType(RepeatedType value);
    total += sizeof(&RepeatedType) == sizeof(void *);
  }
  {
    int EnumShadow(int);
    total += sizeof(&EnumShadow) == sizeof(void *);
  }
  DirectType direct_value = 1;
  NamedType named_value = 1;
  CommentType comment_value = 1;
  SpliceType splice_value = 1;
  PointerReturnType pointer_value = 1;
  ExternType extern_value = 1;
  ParenthesizedType parenthesized_value = 1;
  RepeatedType repeated_value = 1;
  return total == 10 && direct_value + named_value + comment_value +
             splice_value + pointer_value + extern_value +
             parenthesized_value + repeated_value == 8 &&
             EnumShadow == 4
         ? 0
         : 1;
}
