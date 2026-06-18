// _Generic の制御式がスカラ整数キャスト `(char)x` 等のとき、char/short/unsigned char を
// int 扱いして誤マッチしていたバグ。`(T)0` 型トレイト idiom で問題になる。
// 原因: apply_cast が sub-int/int キャストを値計算ノード (`(x<<56)>>56` 等) に lower し、
//      char/short/unsigned char の型幅・符号を AST に残さないため、_Generic の制御型解決が
//      node の type_size を読むと int (4B) に見えていた。
// 修正: parse_generic_selection が「制御式が純粋なスカラ算術型キャスト `(T)operand` で直後が
//      ','」の形を検出し、assoc 型と同じ parse_generic_assoc_type で T を解釈して制御型に
//      使う。ポインタ/関数/タグ型は従来どおり infer に委ねる (複雑型の照合維持)。
// 修正前: char/short/unsigned char キャストが int/default にマッチ
// 期待: exit=42
#include <assert.h>
int main(void){
    int i = 1;
    assert(_Generic((char)i,          char:1, int:0, default:0) == 1);          // char にマッチ
    assert(_Generic((short)i,         short:1, int:0, default:0) == 1);         // short にマッチ
    assert(_Generic((unsigned char)i, unsigned char:1, int:0, default:0) == 1); // unsigned char にマッチ
    assert(_Generic((unsigned)i,      unsigned:1, int:0, default:0) == 1);      // unsigned にマッチ
    assert(_Generic((char)0,          char:1, int:0, default:0) == 1);          // 定数キャストも char
    // 複雑な式は昇格で int (従来どおり infer に委ねる)
    assert(_Generic((char)i + 0,      char:0, int:1, default:0) == 1);          // int にマッチ
    return 0;
}
