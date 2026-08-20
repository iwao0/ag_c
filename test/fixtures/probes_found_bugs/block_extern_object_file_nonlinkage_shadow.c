/* A block-scope extern object may hide a file-scope typedef or enum
 * constant. Its external-linkage backing symbol must not replace the file
 * declaration outside the block (C11 6.2.1p4, 6.2.2p4). */
typedef int DirectObjectType;
typedef int QualifiedObjectType;
typedef int PointerObjectType;
typedef int ArrayObjectType;
typedef int CommentObjectType;
typedef int SpliceObjectType;
typedef int RepeatedObjectType;
typedef int TypeSpecifierObjectType;
enum { EnumObjectName = 4 };

int main(void) {
  int total = 0;
  {
    extern int DirectObjectType;
    total += sizeof DirectObjectType == sizeof(int);
  }
  {
    extern const int QualifiedObjectType;
    total += sizeof QualifiedObjectType == sizeof(int);
  }
  {
    extern int *PointerObjectType;
    total += sizeof PointerObjectType == sizeof(void *);
  }
  {
    extern int ArrayObjectType[];
    total += sizeof(&ArrayObjectType) == sizeof(void *);
  }
  {
    extern /* declaration gap */ int CommentObjectType;
    total += sizeof CommentObjectType == sizeof(int);
  }
  {
    extern int SpliceObjectType\
    ;
    total += sizeof SpliceObjectType == sizeof(int);
  }
  {
    extern int RepeatedObjectType;
    total += sizeof RepeatedObjectType == sizeof(int);
  }
  {
    extern int RepeatedObjectType;
    total += sizeof RepeatedObjectType == sizeof(int);
  }
  {
    extern TypeSpecifierObjectType TypeSpecifierObjectType;
    total += sizeof TypeSpecifierObjectType == sizeof(int);
  }
  {
    extern int EnumObjectName;
    total += sizeof EnumObjectName == sizeof(int);
  }
  DirectObjectType direct_value = 1;
  QualifiedObjectType qualified_value = 1;
  PointerObjectType pointer_value = 1;
  ArrayObjectType array_value = 1;
  CommentObjectType comment_value = 1;
  SpliceObjectType splice_value = 1;
  RepeatedObjectType repeated_value = 1;
  TypeSpecifierObjectType type_specifier_value = 1;
  return total == 10 && direct_value + qualified_value + pointer_value +
             array_value + comment_value + splice_value + repeated_value +
             type_specifier_value == 8 && EnumObjectName == 4
         ? 0
         : 1;
}
