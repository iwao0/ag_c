# 差分テスト バグカバレッジ表

clang との差分テスト（同一 C ソースを ag_c と clang でコンパイルして exit code を比較）で
炙り出した miscompile / コンパイルエラーの **チェック済み領域** を管理する。同じ領域を
何度も探さないための索引。

最終更新: 2026-06-19（可変長無名固定引数 まで）

## 凡例（状態）
- ✅ **済**: チェック済みで現状 green（差分なし）。
- 🔧 **修正済**: バグを発見し修正＋回帰 fixture 登録済み（`test/fixtures/probes_found_bugs/`）。
- ⚠️ **未対応**: バグを確認済みだが未修正（既知の制約）。
- ⬜ **未チェック**: まだ系統的に差分テストしていない。

## 使い方
1. 新しい領域を差分テストする前にこの表を見て、未チェック（⬜）や未対応（⚠️）を優先する。
2. バグを修正したら行を 🔧 に更新し、fixture 名 / コミットを記入する。
3. 「型 × 宣言経路 × 使用文脈」で取りこぼしが多い（例: unsigned を直しても int だけ／local だけ／
   scalar だけ、になりがち）。1 つ直したら long/char/short・global・typedef・配列要素・
   ポインタ deref・struct メンバまで広げて確認し、各セルを更新する。
4. 手法・再現手順は `HANDOFF.md` の「作業のやり方」を参照。

---

## カバレッジ表

### 整数型の幅 / 符号 / キャスト
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| sub-int (char/short) の符号拡張・ゼロ拡張 | 🔧 | cast_short_char_sign_extend ほか | インライン使用で sign/zero-extend |
| (int)/(unsigned)/(signed) キャストの符号更新 | 🔧 | 5b474f7, 1317698 | 全経路網羅済み |
| (int)/(long) の 32/64bit 切り詰め・拡張 | 🔧 | 1c8e358, 8ddafa6 | (long)unsigned の zext 含む |
| long/long long リテラルの i64 型付け | 🔧 | 365d8c0 | |
| i32 比較の 32bit 幅 / sub-int 戻り値切り詰め | 🔧 | int_cmp_width_and_subint_return | |
| unsigned char/short 戻り値のゼロ拡張 | 🔧 | 1b5e1df | |
| unsigned char/short 戻り値の整数昇格（比較の符号） | 🔧 | unsigned_subint_return_promote | funcall に is_unsigned を立てていたため `f()>-1` が unsigned 比較で誤判定。char/short は signed int 昇格なので除外、unsigned int/long のみ保持 |
| typedef 経由 unsigned char/short (`uint8_t`) のロード符号性 | 🔧 | stdheader/stdint_uint8 | adjust_local_decl_spec_from_typedef が typedef の unsigned を上書きして捨て、1byte ロードが ldrsb に。200→-56 (exit truncation で偽装通過) |
| typedef 経由 unsigned char/short の**戻り値**ゼロ拡張 | 🔧 | typedef_unsigned_subint_return | resolve_func_ret_typedef が typedef の unsigned を捨て、sub-int 戻り値が符号拡張 (`u8 f(){return 200;}`→-56)。上記ローカルと同根の戻り型版 |
| typedef unsigned を struct **メンバ**型に使ったときのロード符号性 | 🔧 | typedef_unsigned_struct_member | struct_layout の typedef メンバ分岐が td_isu を member_is_unsigned に反映せず符号拡張 |
| unsigned char/short **配列メンバ** `s.x[i]` のゼロ拡張 | 🔧 | unsigned_char_array_member | build_member_deref_node が配列メンバに pointee_is_unsigned を立てず（is_bool と非対称）。サイズ1配列は array_len=0 に潰れるため subscript で base is_unsigned も伝播 |
| `unsigned long`/`unsigned char` 戻りの符号性追跡 | ⚠️ | — | plain `unsigned` のみ。ret_token_kind が TK_LONG/CHAR に潰れる |
| 混在幅比較（片側 i32・片側 i64） | ⚠️ | — | gen_inst_int_cmp は両 i32 のみ 32bit |
| 複合代入の sub-int / unsigned wrap | ✅ | (probe p3) | |
| 連鎖代入 `b=a=E` の sub-int lvalue 経由の値変換 | 🔧 | chained_assign_narrow_lvalue | 代入式の値が lvalue 型へ変換されず、`b=a=300`(a=uchar) で b が 300 のまま。格納後に再ロードして返す (lvar/gvar/deref/member) |
| シフト境界・unsigned/signed 右シフト | ✅ | (probe p7) | |
| 符号付き除算・剰余（負値, long） | ✅ | (probe p5) | |

### 浮動小数（float / double）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 配列メンバ access/init | 🔧 | float_array_member, 10b9748 | |
| 多次元 subscript の fp load | 🔧 | multidim_float_array_subscript, 10d291d | |
| ブール条件分岐 / `&&` `||` | 🔧 | ec96e30, 87a24a1 | |
| static ローカル float init | 🔧 | f873777 | |
| fp ポインタ仮引数 `double *a` の deref/subscript | 🔧 | fp_pointer_parameter | |
| 匿名 struct/union メンバの fp 昇格 | 🔧 | anon_member_fp_unsigned_promote | |
| 可変長引数の double | ✅ | (probe q2) | |
| double 比較・三項 | ✅ | (probe q5) | |
| NaN / Inf の比較（`nan!=nan`, `1/inf`, denormal） | ✅ | (probe fp1, fp2) | |
| 単項マイナスの -0.0（`-0.0`, `-z`, `1.0/-0.0`=-inf） | 🔧 | fp_unary_minus_neg_zero | ND_FNEG/IR_FNEG。従来は `0.0-x` で符号消失 |
| 負値の浮動小数点グローバル初期化子 `float g=-1.0f` | 🔧 | negative_fp_global_init | psx_eval_const_fp が ND_FNEG 未対応で畳み込み失敗→スカラ .comm(0)/配列要素 0。ND_FNEG 畳み込み＋fp配列要素の非ND_NUM評価を追加 |

### 関数ポインタの FP 戻り値（`d0` で読む）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| funcptr 変数 / 仮引数 / typedef / `(*p)()` | 🔧 | funcptr_fp_return, 0b980b0 | |
| 関数ポインタ配列の要素 `ops[i]()` | 🔧 | funcptr_array_fp_return, 45bd478 | |
| struct メンバ funcptr `s.f()` / `sp->f()` | 🔧 | funcptr_member_fp_return, 20c4b17 | |
| グローバル funcptr `gops()` | 🔧 | funcptr_global_fp_return, ada7696 | |
| グローバル funcptr **配列** `gops[i]()` の fp 戻り (N>=2) | 🔧 | funcptr_global_array_fp_return, e62862e | |
| 要素数 1 の括弧内配列グローバル `(*g[1])()` / `(*g[1])` | 🔧 | global_size1_funcptr_array | paren 内 `[1]` の有無で配列判定。funcptr/ポインタ両方 |
| 間接呼び出しの int→fp 引数昇格 `p(4)` | ⚠️ | — | 仮引数型保存が必要 |
| 可変長プロトタイプの無名固定引数 `int printf(const char*,...)` | 🔧 | variadic_unnamed_proto_fixed_args | 固定引数数 0 誤算→crash。定義なし外部関数で顕在化 |
| 間接呼び出しの int→fp 引数昇格 `fp(3)` | ⚠️ | — | 関数ポインタ経由 `double(*)(double)` 等に int/float 実引数を渡すと昇格されず fp レジスタにゴミ。直接呼び出しは正常 |
| **`void *` 戻り型を void 関数と誤判定** | 🔧 | void_ptr_return | stmt.c の return チェックが ret_token_kind==TK_VOID のみ見て is_pointer 無視→`void *f(){return p;}` が E3005 で弾かれ malloc 風関数が書けず |

### ポインタ / 配列 / subscript
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 多段ポインタ subscript / pql 減算 | 🔧 | d0ff96c, 8e937e0 | |
| struct/union ポインタ算術スケール | 🔧 | b6a42ec | |
| struct ポインタ配列メンバ `h.arr[i]->v` | 🔧 | struct_ptr_array_member_access, db98d34 | |
| 多次元配列の行 decay `m[0]`, `**m`, `*(*(m+1)+2)` | 🔧 | a2a8328, 47975d4 | 3D まで |
| 局所「2D 配列へのポインタ」mid_stride | 🔧 | 5a47279 | |
| 配列へのポインタ（要素 >4B struct） | 🔧 | 10072cc | |
| インラインのポインタキャスト deref `*(int*)(cp+4)` | 🔧 | d44947f | |
| **グローバル data pointer の fp deref** `*dp`/`dp[i]` | 🔧 | global_fp_data_pointer_deref, 426ff01 | |
| **グローバル pointer-to-array** `(*pa)[N]` subscript | 🔧 | global_ptr_to_array_subscript, 9547728 | int/double |
| `(*dp)[j]` 単項 deref + subscript の fp load | 🔧 | ptr_to_array_deref_fp, 8cf0749 | local/global |
| **グローバル多次元 pointee** `(*pa)[N][M]` | 🔧 | global_ptr_to_multidim_array, eb74293 | int/double, 4D まで |
| 変則形 `*(t+N)[K]`（pointer-to-array に subscript+deref 混在） | 🔧 | ptr_array_arith_subscript_deref | `*((t+N)[K])`。make_subscript_scaled_offset に ND_ADD/ND_SUB 分岐を追加し、ポインタ算術 base からポインタ被演算子の inner_deref_size 等を引き継ぐ。これがないと `(t+N)[K]` が配列へ decay せずスカラ load→外側 `*` が値をアドレス deref して SIGBUS。スカラポインタは inner=0 で無影響 |
| typedef 配列へのポインタの stride `typedef int R[3]; R *p` (1D/2D) | 🔧 | typedef_array_pointer_stride | is_pointer+td_array 分岐で outer_stride/mid_stride を設定。pointer_qual_levels を立てない (= 多段ストライド連鎖を使う) ことで 2D `m23 *q; q[i][j][k]` の深い添字も正しく動く |
| struct 配列の部分/0/designator 初期化の 0 補完 | 🔧 | struct_array_partial_init | 部分初期化された struct 要素の残メンバが 0 補完されず garbage / ネスト struct 配列が ir_build 失敗 / `[i].field=` が E2006。配列全体を先に 0 埋め+明示初期化子で上書き、`[i].member` designator もパース対応 |
| sized array 複合リテラルのアドレス `&(int[N]){...}` | 🔧 | addr_of_array_compound_literal | build_unary_addr_node の COMMA 分岐が rhs(既に ADDR)を二重ラップ→ir_build失敗。rhs に & ロジックを再帰適用 |
| 大きいスタックフレーム（>4095B）の `sub sp`/`add x,x29,#off` | 🔧 | large_stack_frame | imm12 上限超を 4096 倍数部(lsl#12)+端数に分割。ldr/str は ~32KB まで別途 |
| ポインタ減算・比較 | ✅ | (probe p6) | |

### 集約初期化（C11 6.7.9）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 多次元配列メンバ / ネスト designator | 🔧 | 4b92768, aadf3b7 | |
| 重複 designator 後勝ち / 位置継続 | 🔧 | 4a5942d, df23e17 | C11 準拠 |
| struct 配列メンバ brace init | 🔧 | 1684a8c | |
| グローバル designator `.member[idx]`/`.member.sub` | 🔧 | global_designator_member_index, 1e843b4 | |
| ローカル designator の struct leaf brace init | 🔧 | local_designator_aggregate_leaf, 7e39081 | |
| `_Bool` 初期化子の 0/1 正規化（全経路） | 🔧 | 5b3d592 | |
| グローバルポインタ配列 `&data[n]`/`data+n` | 🔧 | global_ptr_array_addr_init, 138cd70 | |
| union 配列要素の brace init `arr[2]={[1]={.n=5}}` | ⚠️ | — | local union leaf は E3064 のまま除外 |
| グローバルのネスト brace 配列添字 `{.items={[2]={.a=7}}}` | ⚠️ | — | flat パーサ制約。`.items[2].a=` 形は対応済み |

### struct / union ABI・値渡し
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| 小さい struct（3/5/6/7B）の値渡し/返し | 🔧 | 050e1bf | |
| <=8B struct を ternary / funccall から初期化 | 🔧 | 96b5510, 7697cf8 | |
| static struct/union 局所の永続化 | 🔧 | static_local_struct_persist, 8167e8e | インライン定義の匿名タグは未対応 |
| long bitfield（>32bit / ユニット跨ぎ） | 🔧 | 040da11 | |
| bitfield の符号（signed sign-extend / unsigned）・幅切り詰め | ✅ | (probe u2, u6) | int:4/:1/:12/:20 等 |
| 非 bitfield メンバ直後の bitfield パッキング（`char c; int x:20`） | 🔧 | bitfield_pack_after_member | AAPCS: 収まれば同一 storage ユニットへ |
| pragma pack(1) と通常の混在 sizeof | ✅ | (probe bf3) | |
| ゼロ幅 bitfield `:0` のユニット分離 | ✅ | (probe bf1) | |

| 整数リテラルの sizeof `sizeof(0)` | 🔧 | sizeof_int_literal | psx_node_type_size が ND_NUM に 0 を返し sizeof 既定 8 に落ちて `sizeof(0)`=8。ND_NUM を fp_kind/long サフィックスで判定 (int=4) |

### プリプロセッサ
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `#if` 定数式のビット/シフト/剰余/三項 (`& | ^ << >> % ?:`) | 🔧 | pp_if_operators | 定数式パーサにレベル欠落で `#if (3&5)==1` 等が E1006。conditional/bitor/bitxor/bitand/shift と mul の % を追加 |
| 可変長マクロの空 __VA_ARGS__ `F(a,...)`→`F(42)` | 🔧 | variadic_macro_empty_va | コール側引数チェック `parsed_args <= num_named` が空 va を E1024 で拒否。`< num_named` に緩め名前付き不足時のみエラー (clang/gcc 互換) |

### 型機能 / その他
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `_Alignof` の集約アラインメント | 🔧 | 3e8a4d1 | |
| `sizeof`/`_Alignof` の複数語整数型 `(long long)` 等 | 🔧 | sizeof_multiword_int | parse_parenthesized_type_size が先頭1語のみ消費し `sizeof(long long)` が E2006。整数型指定子列を一括解釈 |
| `_Generic` の文字列/long リテラル/ポインタ種別 | 🔧 | e0b5190 ほか | |
| `_Generic` のスカラ整数キャスト制御式 `(char)x` | 🔧 | generic_scalar_cast_control | `(T)0` idiom。ポインタ/関数/タグは infer 維持 |
| `_Generic` の修飾子除去 / ポインタ段数 | ✅ | (probe gen1) | |
| `_Generic` 深いネスト型 (`(int(*(*)(void))[3])`) の同一型マッチ | ⚠️ | — | clang はマッチ、ag_c は default。複雑型の assoc 照合が不完全 |
| `_Generic` が `long` と `long long` を同一視 | 🔧 | generic_long_vs_longlong | リテラル `0LL`/`0ULL` の LL サフィックス (token int_size) を node→generic_type へ伝播し、同サイズ整数でも long long rank を照合。long long **変数/式** の rank 追跡は宣言・式型経路の別課題として残る |
| `_Generic` が引数違いの関数ポインタを同一視 | ⚠️ | — | `int(*)(int)` と `int(*)(int,int)` を区別せず最初の funcptr assoc にマッチ。修正には関数シグネチャ型モデリングの新規追加が必要（parse_funcptr_abstract_decl は param を skip、funcptr typedef/lvar に仮引数情報フィールド無し、control/assoc 両側に保持＋matcher 比較を横断追加）。実用ほぼ皆無のため意図的に未対応 |
| `_Generic` の long long **変数/式** の rank | ⚠️ | — | リテラル `0LL` は generic_long_vs_longlong で対応済み。変数・式の型は long long rank を持たず long にマッチ（宣言経路・式型推論に long long フラグ追加が要る） |
| enum 値・算術 | ✅ | (probe q3) | |
| switch / fallthrough / default | ✅ | (probe p4) | |
| 複合代入演算子 `<<=` `>>=` `&=` `|=` `^=` `%=`（型幅・符号） | ✅ | (probe ca1, ca2) | sub-int/メンバ/ポインタ deref/配列要素 |
| do-while / goto / 多重代入 / 前後置インクリメント | ✅ | (probe q4, q7) | |
| compound literal（引数・式中） | ✅ | (probe q6) | |
| ファイルスコープ複合リテラル初期化子 `int x=(int){42};` | ⚠️ | — | **先行する関数宣言**があると初期化子が失われ x=0（clang=42）。グローバル変数の先行では起きない。assert.h 取込で顕在化 |
| ファイルスコープ `static struct/union/enum var` | 🔧 | static_tag_global | parse_toplevel_decl_spec が tag 判定前に storage class を skip せず、`static struct S g` を E3016 で拒否。tag 前に修飾子先読み skip を追加 |
| `void *` 戻り型を void 関数と誤判定 | 🔧 | void_ptr_return | (整数型の節参照) return チェックが is_pointer 無視 |
| 間接呼び出しの int→fp 引数昇格 `fp(3)` | ⚠️ | — | (関数ポインタ節参照) 関数ポインタ変数が仮引数型を保持せず昇格できない |
| 可変長引数（int） | ✅ | (probe q1) | |
| volatile スカラ/ポインタの値（`volatile int`, `*p+=`） | ✅ | (probe u3) | 値の正しさのみ。順序保証は別 |
| 文字/エスケープリテラル（`\n` `\0` `\x41` `\101`） | ✅ | (probe u4) | |
| char/short 実引数の int 昇格（可変長経由） | ✅ | (probe u5) | |
| 非可変長の char/short/float 実引数昇格 | ✅ | (probe prom_check) | float→double 昇格含む |
| `restrict` 修飾ポインタ（値の正しさ） | ✅ | (probe restrict_check) | |
| `_Atomic` キーワード（`_Atomic int`/`long` の値） | ✅ | (probe atomic2) | stdatomic.h は同梱なし |
| ワイド文字/文字列リテラル `L'A'` `L"hi"[i]` | ✅ | (probe wide2) | int/long 値として動作 |
| stddef.h の wchar_t / max_align_t（C11 7.19） | 🔧 | stddef_wchar_t, stddef_max_align_t | wchar_t=int(4B), max_align_t=long double(8/8) |
| 戻り値の暗黙変換（int↔double, char/short/unsigned 切り詰め） | ✅ | (probe ret_conv, ret_conv2) | |
| VLA（1D / 2D / 関数引数 / sizeof 全体） | ✅ | (probe vla1, vla2, vla4) | |
| VLA を subscript した `sizeof(a[0])` | 🔧 | sizeof_vla_subscript | `sizeof(a)` 全体は元から動作 |

---

## バグではない（仕様 / 既知の差異、追わない）
- statement expression `({...})`（GNU 拡張、非標準）。
- 過剰初期化子 `struct S s={{1,2},{3,4}}`（メンバ1個に2グループ）等は ag_c は意図的に E3064（厳格）。
- `s07.c`（深さ 10 万の再帰）の SIGSEGV はスタックオーバーフロー（誤コンパイルではない）。
- 評価順序が未規定/UB のもの（`a[i++]=i` 等）。
- 同梱ヘッダに `complex.h` が無く `#include <complex.h>` が E1034（`_Complex` 言語機能自体は動く）。
- 同梱ヘッダに `stdatomic.h` が無く `#include <stdatomic.h>` が E1034（`_Atomic` 言語機能自体は動く）。

## 未チェックの着手候補（⬜ を埋める）
- `_Generic` 深いネスト型の同一型マッチ（上表 ⚠️、複雑型 assoc 照合の改善）。
- 複数 translation unit / extern リンケージの差分（現状の差分テストは単一ファイル）。
- 浮動小数の `printf` 出力書式（%f/%g/%e の丸め）。

## チェック済みだが miscompile でなかった領域（再探索不要）
- compound literal: ネスト / `&(compound)` / 配列 / 関数引数 (probe cl1, cl3)。
- `static p = &(struct){..}`（非定数 static init）は clang が拒否、ag_c は受理（寛容な差異）(probe cl2)。
