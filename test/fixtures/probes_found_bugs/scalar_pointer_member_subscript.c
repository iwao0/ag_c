// struct メンバの char* (スカラポインタ) に対する subscript
// 修正前: build_member_access は配列メンバのみ deref->is_pointer を立て、
// スカラポインタメンバ (`char *name`) では is_pointer=0 のままだった。
// 結果、`s.name[0]` の subscript で base アドレス計算 ND_ADD をそのまま
// base に使い、メンバ slot の下位 1 byte (= ポインタ値の LSB) を読んでいた。
//
// build_member_access でスカラポインタメンバに is_pointer + is_scalar_ptr_member
// を立て、subscript_base_address_of が ND_DEREF をそのまま返して
// ポインタ値の load を引き起こすように修正。配列メンバの decay 表現とは
// is_scalar_ptr_member フラグで区別 (union の `char b[4]` メンバを壊さない)。
#include <assert.h>
struct S { char *name; int val; };
int main(void) {
  struct S s = {"foo", 42};
  assert(s.name[0] == 'f'); assert(s.val == 42); return 0; // 'f'+42 = 102+42 = 144
}
// 期待: 144
