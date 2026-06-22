# 差分テスト バグカバレッジ表

clang との差分テスト（同一 C ソースを ag_c と clang でコンパイルして exit code を比較）で
炙り出した miscompile / コンパイルエラーの **チェック済み領域** を管理する。同じ領域を
何度も探さないための索引。

最終更新: 2026-06-21（タグ戻り複雑宣言子まで）

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
| `unsigned long`/`unsigned char` 戻りの符号性追跡 | ✅ | unsigned_long_return_signedness | 旧 ⚠️。再検証で shift/divide/比較いずれも clang 一致＝再現せず。回帰 fixture 化 |
| 混在幅比較（片側 i32・片側 i64） | ✅ | mixed_width_comparison | 旧 ⚠️。混在は 64bit で sign/zero-extend して比較され正しい（miscompile でない）。回帰 fixture 化 |
| 複合代入の sub-int / unsigned wrap | ✅ | (probe p3) | |
| 連鎖代入 `b=a=E` の sub-int lvalue 経由の値変換 | 🔧 | chained_assign_narrow_lvalue | 代入式の値が lvalue 型へ変換されず、`b=a=300`(a=uchar) で b が 300 のまま。格納後に再ロードして返す (lvar/gvar/deref/member) |
| 三項 `c?int:char` の sub-int 分岐の値 | 🔧 | ternary_subint_branch | char 分岐を 4 バイト slot に strb で書き上位 garbage→merge ldrsw が誤値。結果型へ retag して full-width store (signed/unsigned 双方正しい) |
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
| 同梱 `<complex.h>` (I/creal/cimag/conj/cabs/carg、算術) | 🔧 | stdheader/complex_ops | _Complex brace 初期化・複素数 compound literal で I={0,1}、creal/cimag は __real__/__imag__ ベース (rvalue 可)、cabs/carg は <math.h> 経由。算術はネイティブ。値渡し無し |
| GNU `__real__` / `__imag__` 演算子 | 🔧 | real_imag_operators | 複素数式を temp slot に materialize し re/im を fp load。実数は x/0。rvalue 可 |
| `double _Complex` ↔ `float _Complex` 変換 | 🔧 | complex_float_double_convert | build_complex_to が成分の fp 種別を無視して memcpy し虚部が壊れていた。源 fp で materialize→各成分 F2F 変換 |
| `_Complex` の値渡し / 値返し ABI | 🔧 | complex_by_value_abi | HFA として re→d0/s0, im→d1/s1 で受渡し。param 受取(2 IR_PARAM)・return・呼出引数(2 FP 展開)・戻り値受取(d0/d1→slot) を実装。double complex はポインタ渡し、float complex は 1 レジスタで虚部喪失していた |
| 複素数初等関数 cexp/clog/csqrt/cpow/csin/ccos/ctan 等 | 🔧 | stdheader/complex_ops | 値渡し/値返しが効くので static 関数で実装。実部/虚部は <math.h> 経由。libm 同名を static で隠し ag_c/clang 同一実装で一致 |
| 本物の `<stdatomic.h>` (LSE アトミック命令) | 🔧 | stdheader/stdatomic_ops | 単一スレッド退化版から、Apple ARM64 LSE 命令 (ldaddal/ldsetal/ldclral/ldeoral/swpal/casal/ldar/stlr/dmb ish) による真のアトミックへ。fetch 系は規格通り旧値を返す。__ag_atomic_* 組込み + IR_ATOMIC + 1/2/4/8 バイト codegen。全 seq_cst 強度 |
| **fp 実引数 → 整数仮引数** の暗黙変換 `ei(7.9)` (直接呼出) | 🔧 | fp_arg_to_int_param | C11 6.5.2.2p7。ag_c は double/float 実引数を fp レジスタ(d0)に置いたまま callee を呼び、callee が整数レジスタ(x0/w0)から読みゴミ値に。callee の整数スカラ仮引数幅 (4/8) を semantic_ctx に記録 (param_int_sizes、既存 param_fp_kinds と排他) し、呼び出し側 build_node_funcall で実引数が fp なら F2I(fcvtzs) を挿入。long 仮引数は i64 幅で変換 (i32 だと大値が wrap)。同一 TU 定義関数で有効。**符号性は fcvtzs 固定** (fp→unsigned の大値は既存 F2I の制約) |

### 関数ポインタの FP 戻り値（`d0` で読む）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| funcptr 変数 / 仮引数 / typedef / `(*p)()` | 🔧 | funcptr_fp_return, 0b980b0 | |
| 関数ポインタ配列の要素 `ops[i]()` | 🔧 | funcptr_array_fp_return, 45bd478 | |
| struct メンバ funcptr `s.f()` / `sp->f()` | 🔧 | funcptr_member_fp_return, 20c4b17 | |
| グローバル funcptr `gops()` | 🔧 | funcptr_global_fp_return, ada7696 | |
| グローバル funcptr **配列** `gops[i]()` の fp 戻り (N>=2) | 🔧 | funcptr_global_array_fp_return, e62862e | |
| 要素数 1 の括弧内配列グローバル `(*g[1])()` / `(*g[1])` | 🔧 | global_size1_funcptr_array | paren 内 `[1]` の有無で配列判定。funcptr/ポインタ両方 |
| 間接呼び出しの int→fp 引数昇格 (直書き funcptr) `fp(3)` | 🔧 | funcptr_int_to_fp_arg | 宣言時に skip_func_params で各仮引数の fp 種別を funcptr_param_fp_mask に記録し、parse_call_postfix で fp 仮引数の実引数を wrap_to_fp(ND_INT_TO_FP) でラップ。float/double/混在/(*fp)() 対応。**typedef 経由 funcptr は未対応** (typedef 側に mask 保存が必要) |
| 可変長プロトタイプの無名固定引数 `int printf(const char*,...)` | 🔧 | variadic_unnamed_proto_fixed_args | 固定引数数 0 誤算→crash。定義なし外部関数で顕在化 |
| 間接呼び出しの int→fp 引数昇格 (直書き funcptr) | 🔧 | funcptr_int_to_fp_arg | 上記参照。直書き funcptr のみ。typedef 経由は未対応 |
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
| typedef 自体が配列へのポインタ `typedef int (*PA)[3]; PA p` (局所使用) | 🔧 | typedef_ptr_to_array | 上の `R *p` と別経路。typedef 定義 (toplevel: parser.c / 関数内: stmt.c) が is_ptr のとき括弧の後ろの `[3]` (ポインティ extent) を捨てていて、`p+1`/`p[i]` が 1 行でなく要素 1 個 (4B) しか進まなかった。`*` が括弧内 (ptr_in_paren) のとき `[3]` をポインティ dims として記録し、resolve_typedef_array_dims の is_array ゲートを外して宣言側の `is_pointer && td_array_dim_count>0` 分岐に乗せる。多次元ポインティ `(*PB)[2][3]` も対応 |
| ポインタ typedef を基底にした**グローバル変数** `typedef int *PI; PI gp` | 🔧 | global_pointer_typedef | gp が int スカラ登録 (sizeof=4, `gp[i]` で E3064)。parse_toplevel_decl_after_type のオブジェクト経路が base_is_ptr を 0 固定で宣言子へ渡し、typedef 基底のポインタ性を捨てていた (typedef 経路は渡していた)。`g_toplevel_decl_base_is_ptr` を渡すよう修正。int/char/long/unsigned/double*・struct ポインタ・pointer-to-array (PA/PB) を網羅。double は pointee fp_kind を実効段数で判定して伝播、pointer-to-array typedef グローバルは typedef のポインティ dims から outer_stride を設定 (直書き `int *gp`/`int(*gp)[3]` は宣言子の `*` で立つので不変) |
| 多段ポインタ typedef `typedef int **PP; PP p` (局所/仮引数) | 🔧 | multilevel_pointer_typedef | typedef がポインタ段数を bool でしか持たず `**p` が誤 deref→SIGSEGV。typedef_name_t に pointer_levels を追加し、定義時 (toplevel: parser.c / 関数内: stmt.c・decl.c) に「基底段数+宣言子 prefix `*` 数」を 2 段以上だけ記録、宣言時 (decl.c) に getter で取得して total_pointer_levels/pql に反映。直書き `int **p` と同一ノード属性になる。2/3 段・合成 `typedef PI *PP2`・仮引数を網羅 |
| グローバル変数の多段ポインタ `int **gp; **gp` (直書き/typedef経由) | 🔧 | global_multilevel_pointer | 第1 deref `*gp` が int* (8B) でなく int (4B, ldrsw) ロードされ、続く deref が壊れた値を deref→SIGSEGV。register_toplevel_global_decl がポインタ deref_size を常に要素サイズにし、global_var_t が段数を持たなかった。global_var_t に pointer_qual_levels を追加 (宣言子 `*` 数 + 基底ポインタ typedef 段数)、try_build_global_var_node が pql>=2 のとき参照ノードに deref_size=8/base_deref_size=要素/pql を立てローカル `int **lp` と同一表現に。int/char/struct・3段・`(*gp)[i]` 添字・`*gp=` 代入を網羅 |
| 多段ポインタの fp pointee `double **p; **p` / `(*pp)[i]` 配列 decay 添字 | 🔧 | multilevel_pointer_fp_pointee | fp_kind が多段ポインタへ伝播せず float がゴミ・double 書き込みが落ちていた。(1) 宣言時に多段ポインタへも最内 pointee_fp_kind を設定 (旧: total_pointer_levels==1 のみ)、(2) build_unary_deref_node / build_subscript_deref が pql を 1 段消費するとき pointee_fp_kind を結果へ引き継ぐ。double/float の read/write・3段・`*pp[i]`・`(*pp)[i]` を網羅 |
| struct 配列の部分/0/designator 初期化の 0 補完 | 🔧 | struct_array_partial_init | 部分初期化された struct 要素の残メンバが 0 補完されず garbage / ネスト struct 配列が ir_build 失敗 / `[i].field=` が E2006。配列全体を先に 0 埋め+明示初期化子で上書き、`[i].member` designator もパース対応 |
| sized array 複合リテラルのアドレス `&(int[N]){...}` | 🔧 | addr_of_array_compound_literal | build_unary_addr_node の COMMA 分岐が rhs(既に ADDR)を二重ラップ→ir_build失敗。rhs に & ロジックを再帰適用 |
| ファイルスコープのスカラ複合リテラルのアドレス `int *p=&(int){5};` | 🔧 | file_scope_addr_of_compound_literal | B6。ファイルスコープのスカラ複合リテラルが値 (ND_NUM) に短絡され `&` がアドレスを解決できず `.comm`(0)→`*p` が NULL deref で SIGSEGV。C11 6.5.2.5 で静的記憶域なので、`&` 配下のとき (g_addr_of_compound_pending) は gvar 実体を生成しアドレス初期化。int/long/char/unsigned/double/float・ポインタ配列要素を網羅。**根因の副次バグも修正**: 関数プロトタイプ/定義後に g_current_funcname を NULL に戻しておらず、`<assert.h>` の `__assert_rtn` 宣言後にファイルスコープ複合リテラルが「関数内」と誤判定されローカル lvar 経路に乗っていた |
| ファイルスコープの **struct/配列** 複合リテラルのアドレス `&(struct S){...}` | 🔧 | file_scope_aggregate_compound_literal_addr | ファイルスコープ分岐が単一スカラ (psx_expr_assign 1 個) しか扱えず `,` で E2006。集約 (struct/union/配列) のとき gvar 実体を作り、グローバル struct/配列と同じ psx_parse_global_brace_init_flat で brace 初期化を展開してアドレス可能なノードを返す分岐を追加。init_fvalues も確保して fp 配列に対応。struct/designator/ネスト/union/char配列/int・double 配列を網羅 |
| 大きいスタックフレーム（>4095B）の `sub sp`/`add x,x29,#off` | 🔧 | large_stack_frame | imm12 上限超を 4096 倍数部(lsl#12)+端数に分割。ldr/str は ~32KB まで別途 |
| ポインタ減算・比較 | ✅ | (probe p6) | |
| **ポインタ戻り関数の subscript/算術** `g()[i]` / `*(g()+i)` | 🔧 | func_pointer_return_subscript | parser がポインタ戻り値の pointee 型を覚えず ND_FUNCALL の deref_size=0。subscript/算術がスケールせず 1 バイト加算で miscompile (`int* g(); *(g()+3)` が 5120)、`double* g()` は戻り値を d0 から誤読し SIGSEGV、`unsigned char* g()` は符号拡張。修正: (1) semantic ctx の戻り値型 (tag/token_kind) から pointee サイズ/fp 種別を導出し ND_FUNCALL の deref_size/type_size/pointee_fp_kind アクセサに反映、(2) ポインタ返しの fp_kind を funcall ノードに立てない (戻り値は x0)、(3) 基底型 unsigned を ctx に保存し subscript の zero-extend に伝播 (戻り値そのものの符号化は is_pointer でガード)。int/long/char/unsigned char/short・double/float・struct(.member/->)・void* cast・ポインタ算術 (+/-)・変数添字を網羅 |
| **配列へのポインタを返す関数** `int (*f())[N]` の `f()[i][j]` | 🔧 | func_return_pointer_to_array | parse_func_declarator が pointee 配列次元 `[N]` を読み飛ばし、call ノードに行ストライド (N*elem) が無く base 要素 (4) で誤スケール→SIGSEGV (`int(*p)[N]=f()` 経由は動作)。semantic ctx に先頭次元 N を記録 (ret_pointee_array_first_dim)、ps_node_deref_size(ND_FUNCALL)=N*elem、subscript inner_ds=elem、build_unary_deref_node が `*f()` を要素サイズで解決。f()[i][j]・(*f())[j]・(*(f()+k))[j]・write・引数あり・double を網羅。単一次元のみ (多次元 `[N][M]` 戻りは未記録のまま) |

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
| >8B struct の複合リテラル代入 `s=(struct S){...}` | 🔧 | compound_literal_struct_assign | ir_build_module failed。build_assign_struct が src に ND_COMMA(init,temp) 形を扱わず fail。init 評価後 temp を struct ソースに |
| 2D char 配列の文字列リスト初期化 `char a[2][6]={"hello","world"}` | 🔧 | char_2d_array_string_init | 各文字列がスカラ/ポインタ扱いで行を埋めず壊れる。ローカル/グローバル両経路で文字列を行 (row 幅) のバイト列に展開し残り 0 埋め |
| union 配列要素の brace init `arr[2]={[1]={.n=5}}` | 🔧 | union_array_brace_init | 旧 ⚠️(E3064)。E3064 は解消済みだが今度は値が格納されず 0 に化けていた (miscompile)。parse_array_elem_struct_brace_init が要素を常に parse_struct_initializer へ投げ、union 要素の `.n=5` を struct レイアウトで誤解決していた。要素が union のとき parse_union_initializer へ委譲。designator/positional/混在メンバ/部分初期化を網羅 |
| グローバルのネスト brace 配列添字 `{.items={[2]={.a=7}}}` | 🔧 | global_nested_brace_designator | flat 初期化パーサがネスト brace の再帰に「その level の集約型」コンテキストを渡さず、`.member` designator を常に最外 gv の tag で解決して E3064。型コンテキスト gbrace_ctx_t を再帰へ渡し ctx に対して解決するよう変更。併せて (a) ネスト配列 `[N]=` の絶対 slot に level 起点を加算、(b) 配列レベル positional 要素の境界整列 (部分初期化 `{.a=1}` 後のズレ) も修正。ネスト struct `.s={.a}`・配列添字・positional 混在・fp メンバ網羅 |

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
| enum 型 bitfield `enum E e:2` の符号 | 🔧 | bitfield_enum_and_static_init | tag-keyword 分岐で is_signed_type=1 のままで符号拡張し最上位ビット値が負に。非負列挙は unsigned 扱い (clang と同じ) |
| グローバル/静的 struct の bitfield 初期化子 | 🔧 | bitfield_enum_and_static_init | 同一 storage ユニットの先頭フィールドしか出力せず残り 0 (`{3,5}`→`.long 3/.long 5` で 0x53 でなく)。同一ユニットの bitfield を 1 整数に詰めて出力 |

| 整数リテラルの sizeof `sizeof(0)` | 🔧 | sizeof_int_literal | psx_node_type_size が ND_NUM に 0 を返し sizeof 既定 8 に落ちて `sizeof(0)`=8。ND_NUM を fp_kind/long サフィックスで判定 (int=4) |
| 文字列リテラルの sizeof がエスケープを生バイト数で数える / `sizeof(&array)` | 🔧 | sizeof_string_and_addr_of_array | `sizeof("\t")`=3 (raw 2+1)→デコード後長で。併せてグローバル `char g[]="a\tb"` の未デコード格納も修正。`sizeof(&arr)` は要素サイズ→`int(*)[N]` なので 8 |

### プリプロセッサ
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `#if` 定数式のビット/シフト/剰余/三項 (`& | ^ << >> % ?:`) | 🔧 | pp_if_operators | 定数式パーサにレベル欠落で `#if (3&5)==1` 等が E1006。conditional/bitor/bitxor/bitand/shift と mul の % を追加 |
| 可変長マクロの空 __VA_ARGS__ `F(a,...)`→`F(42)` | 🔧 | variadic_macro_empty_va | コール側引数チェック `parsed_args <= num_named` が空 va を E1024 で拒否。`< num_named` に緩め名前付き不足時のみエラー (clang/gcc 互換) |
| 文字列リテラルと stringize 結果の連結 `"a" S(b)` | 🔧 | string_concat_stringize | stringize 結果は char_width=0。parse_string_literal_sequence の幅比較が 0 を正規化せず 2番目以降で E3002。比較側も 0→CHAR 正規化 |
| `#` stringize が文字列リテラル引数のクォート/エスケープを落とす | 🔧 | stringize_string_literal | `STR("hi")` が `"hi"` でなく hi に。C11 6.10.3.2 通り囲み `"` を保持し内部の `"`/`\` の前に `\` を挿入 (token_text は引用符なし内容を返すため再構築) |
| 空のマクロ実引数 `F(7,)` / `F(,8)` / `F(a,,c)` | 🔧 | empty_macro_argument | C99 6.10.3p4 で空引数は合法 (placemarker) なのに has_empty_arg で E1024 拒否。引数個数チェックは残し空引数自体ではエラーにしない |
| `#if 0` 内の非 C トークン (`@` `$`・未終端リテラル・不正数値) の skip | 🔧 | if0_skip_non_c_tokens | 全体 tokenize 後に preprocess する構造のため、偽分岐内の未知文字・未終端リテラル (`don't` の `'`)・不正数値が tokenize 時に E2028/E2018 で先に落ちていた。寛容モードを導入: プリプロセッサが pps_skip_cond_incl と pps_materialize_line の行末先読みの間だけ tk_set_tolerate_untokenizable(true) を立て、tk_stream_next が setjmp を張る。scanner の各種 TK_DIAG_* は寛容モード中のみ tk_tolerate_longjmp_if_active 経由で longjmp し、当該トークンを 1 文字の TK_UNKNOWN にして進める (skip が捨てる)。over-pull で active に漏れた TK_UNKNOWN は pps_step が E2028。active コードの不正は従来どおり即エラー (位置も正確) |

### 型機能 / その他
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `_Alignof` の集約アラインメント | 🔧 | 3e8a4d1 | |
| `sizeof`/`_Alignof` の複数語整数型 `(long long)` 等 | 🔧 | sizeof_multiword_int | parse_parenthesized_type_size が先頭1語のみ消費し `sizeof(long long)` が E2006。整数型指定子列を一括解釈 |
| `_Generic` の文字列/long リテラル/ポインタ種別 | 🔧 | e0b5190 ほか | |
| `_Generic` のスカラ整数キャスト制御式 `(char)x` | 🔧 | generic_scalar_cast_control | `(T)0` idiom。ポインタ/関数/タグは infer 維持 |
| `_Generic` の修飾子除去 / ポインタ段数 | ✅ | (probe gen1) | |
| `_Generic` 深いネスト型 (`(int(*(*)(void))[3])`) の同一型マッチ | 🔧 | generic_complex_derived_type{,_global} | 構造的フィールドでは区別不能だったため、型を正規化トークン文字列にして照合 (control が局所変数 / キャスト / グローバル変数のとき有効)。control/assoc 双方に sig があれば strcmp、片方のみは従来照合 (加算的) |
| `_Generic` が `long` と `long long` を同一視 | 🔧 | generic_long_vs_longlong | リテラル `0LL`/`0ULL` の LL サフィックス (token int_size) を node→generic_type へ伝播し、同サイズ整数でも long long rank を照合 |
| `_Generic` が `char`/`signed char`、`long`/`long long` を**変数**で同一視 | 🔧 | generic_char_and_longlong_identity | 制御式が変数だとサイズと符号しか持たず誤マッチ。is_long_long / is_plain_char の型識別を宣言 (psx_consume_type_kind の side-channel)→lvar→参照ノード→infer_generic_control_type へ伝播し generic_type_matches で照合。assoc 側は parse_integer_cast_spec_sequence が is_plain_char を出力 |
| `_Generic` が int 制御式を単一 int メンバの struct に誤マッチ | 🔧 | generic_struct_vs_scalar | generic_type_matches が control 側しか tag を見ず、`_Generic((int), Anon:.., int:..)` がサイズ一致でスカラ経路で Anon に化けた。どちらか一方でも tag を持てば tag 一致を要求 |
| `_Generic` が引数違いの関数ポインタを同一視 | 🔧 | generic_complex_derived_type{,_global} | `int(*)(int)` と `int(*)(int,int)`、`int(*)(int)` と `double(*)(int)` を区別。型を正規化トークン文字列化して比較するので引数の個数・型・戻り型まで照合する。制御式が局所変数 / キャスト `(T)0` / グローバル変数のいずれでも有効 |
| `sizeof`/`_Alignof(enum E)` | 🔧 | sizeof_enum_type | parse_parenthesized_type_size が struct/union/typedef のみ対応し enum 型名を E3064 で拒否。enum は int 相当 4B として TK_ENUM 分岐を追加 (sizeof(enum 変数) は元から動作) |
| **`static` ローカル配列の `sizeof`** `static int a[10]` | 🔧 | static_local_array_sizeof | sizeof が要素サイズ(4)を返していた。static ローカル配列は try_lower_static_local_array でグローバルへ lowering され alias lvar が is_array=0/size=0 で登録されるため、parse_sizeof_operand の `arr_var->is_array` 特別処理を素通りし一般経路 ND_ADDR(ND_GVAR) の type_size=要素ストライド(4) になっていた。lvar_is_static_local_array のとき lowering 先グローバルの type_size (=全体サイズ) を返す分岐を追加。int/char/double・`sizeof(a)/sizeof(a[0])` イディオム |
| `_Alignas(>16)` のローカル変数 | 🔧 | alignas_overaligned_local | x29 が 16 整列のみで固定オフセットでは過剰整列不可。align_bytes>16 のローカルだけ予備領域+実行時丸め (IR_ALIGN_PTR=add #A-1;and #-A)。併せて `_Alignas(N) struct ...` 局所の (N) を読み飛ばせず E3015 だったパースも修正 |
| enum 値・算術 | ✅ | (probe q3) | |
| switch / fallthrough / default | ✅ | (probe p4) | |
| 複合代入演算子 `<<=` `>>=` `&=` `|=` `^=` `%=`（型幅・符号） | ✅ | (probe ca1, ca2) | sub-int/メンバ/ポインタ deref/配列要素 |
| do-while / goto / 多重代入 / 前後置インクリメント | ✅ | (probe q4, q7) | |
| compound literal（引数・式中） | ✅ | (probe q6) | |
| ファイルスコープ複合リテラル初期化子 `T g=(T){...};` | 🔧 | file_scope_compound_literal_init | struct で `,` で E2006、scalar で先行関数宣言があると x=0。`T g=(T){...}` は `T g={...}` と等価 (C11 6.5.2.5) なので先頭の `(型)` を読み飛ばし既存 brace 初期化へ。scalar/array/struct 対応 |
| ファイルスコープ `static struct/union/enum var` | 🔧 | static_tag_global | parse_toplevel_decl_spec が tag 判定前に storage class を skip せず、`static struct S g` を E3016 で拒否。tag 前に修飾子先読み skip を追加 |
| **storage class 付きタグ戻り関数** `static struct S *f(){...}` | 🔧 | static_tag_return_function | is_toplevel_function_signature と parse_func_decl_spec がどちらも「タグキーワードの前の storage class (static/extern)」を飛ばさず、`static struct S *g()` がオブジェクト宣言と誤判定 (E2006 `;` 期待) / 戻り型が implicit int に化け E3064。`static int *g()`(builtin) や非 static `struct S *g()` は動作。両関数で storage class の直後がタグキーワードなら storage class を消費してタグ経路へ。struct/union/enum・ポインタ/値返し・引数あり・戻り値 subscript を網羅 |
| **ファイルスコープ `static <typedef名> 変数`** `static Point p;` | 🔧 | static_typedef_name_global | parse_toplevel_decl_spec が storage class (static/extern) を tag キーワードの前でしか skip せず、typedef 名の前では skip しなかったため `static Point p` で `static` が残り Point が型と認識されず E2006 (`;` 期待)。非 static や builtin (`static int`) や tag (`static struct S`) は動作。typedef 名の前にも修飾子先読み skip を効かせる (toplevel_prefix_precedes_typedef_name)。const 修飾・ポインタ・配列・named/anon typedef・関数戻りを網羅 |
| **const/volatile 付きポインタ戻り型** `int *const f()` | 🔧 | qualified_pointer_return | is_toplevel_function_signature の先読みと parse_pointer_suffix_flags が `*` の後の const/volatile を飛ばさず、関数と認識されずオブジェクト宣言と誤判定して E2006 (`;` 期待)。`const int *f()` (pointer to const) は動作。lookahead (3 関数の `*` skip ループ) と suffix 解析の両方で `*` の後の const/volatile を読み飛ばす。int/double/struct・const/volatile・subscript 併用を網羅 |
| **多段ポインタを返す関数の直接 deref** `int **g(); **g()` | 🔧 | multilevel_pointer_return | semantic ctx の ret_is_pointer が bool で段数を持たず `int **` を単段 `int *`(pointee 4B) 扱い→`*g()` が int になり `**g()` が int 値をアドレス参照で SIGSEGV。型付き変数経由 `int **q=g(); **q` は元から動作。戻り型の `*` 段数を ret_pointer_levels に記録し、node_utils の funcall 経路 (pointer_qual_levels / base_deref_size / ps_node_deref_size) が段数>=2 のとき `*g()` を「1 段減らしたポインタ」(8B 値・最内基底型 deref) として組む。build_subscript_deref も funcall base を 1 段消費 (`g()[i]`→int*)。int**/char**/int***・prefix deref・deref+subscript 混在 `(*rg())[1]`・直接 subscript `rg()[0][i]` を網羅。単段ポインタ戻りは不変 (段数>=2 ゲート)。`int *const *f()` 等の qualified 多段もこれで解禁 |
| **タグ戻り + `(*...)` 宣言子** `struct P (*f())[3]` / `struct R (*f())(int)` | 🔧 | tag_return_complex_declarator | is_tag_return_function_signature が `(*...)` 宣言子を扱わず (`(` 後に IDENT を期待し `*` で return 0)、変数宣言と誤判定して E2006。builtin `int (*f())[3]` は is_toplevel_function_signature が処理できていた。両関数の宣言子判定を共有ヘルパ is_function_declarator_sig に抽出し、タグ戻りでも配列へのポインタ戻り・関数ポインタ戻りを検出 |
| **struct を返す関数ポインタの間接呼び出しメンバアクセス** `op(41).v` / `op(41)->v` | 🔧 | funcptr_return_struct_member | 間接呼び出し (callee != NULL) の funcall ノードに戻り tag 型が伝播せず psx_node_get_tag_type が TK_EOF を返し E3005 (`.`/`->` の左辺が構造体でない)。直接呼び出し `mk(41).v` は ret_tag 表から引けて動作。callee の funcptr 変数は tag フィールドに戻り tag を保持するので導出し、戻り値がポインタか否かは pointer_qual_levels で判定 (値戻り `struct R (*op)()`=pql1→ptr0 / ポインタ戻り `struct R *(*op)()`=pql2→ptr1)。値戻り/ポインタ戻り・deref形 `(*op)(x).v`・global funcptr・union 戻り・8B ネストメンバ連鎖を網羅。1/2/4/8B のレジスタ返し限定 (非 pow2 サイズは下行で対応) |
| **1/2/4/8B 以外の struct を返す関数ポインタの間接呼び出し** `struct Big (*ob)(int); ob(100).a` | 🔧 | funcptr_return_large_struct | x8 ret_area 間接返し ABI を direct call 限定で実装しており、間接呼び出しは IR build 失敗 ("ir build/emit failed")。メンバアクセス以前に `struct Big r=ob(100);` 単独でも落ちていた。直接呼び出し `mkbig(100).a` は動作。3 箇所を修正: (1) parse_call_postfix が間接 funcall ノードに ret_struct_size を未設定 (0) → callee funcptr の戻り tag (pql<=1 値戻り) からサイズ導出。(2) build_assign_struct が間接 struct 戻りを明示 fail → 汎用 funcall 経路へ委譲し ret_area から dst へ memcpy。(3) build_node_funcall の ret_area 確保が `!fn->callee` 限定 → direct/indirect 両方で確保 (codegen は x8 設定と blr を独立に出す)。12B/16B/20B struct・16B union・変数代入・直接メンバ・deref形・global funcptr・間接戻り値の値引数渡しを網羅 |
| **ポインタ typedef 仮引数の subscript** `typedef char* Str; len(Str s){ s[i] }` | 🔧 | pointer_typedef_param_subscript | param_decl_spec_t が typedef のポインタ性 (_ti.is_pointer) を捕捉せず、宣言子に `*` が無い (param_is_ptr=0) ため register_param_lvar のポインタ分岐に入らずスカラ登録され `s[i]` が E3064。deref `*s` は build_unary_deref が 8B 値を寛容に許し動作、直書き `const char* s` も動作。typedef 基底のポインタ段数を捕捉し宣言子の `*` と合成して実効ポインタ性を決める。char*/int*/const char*/struct*・多段 typedef・typedef+宣言子 `*`・ポインタ算術・添字代入を網羅 |
| **unsigned char/short ポインタ経由の zero-extend** `unsigned char* p; p[i]` | 🔧 | unsigned_char_pointer_zero_extend | pointee が符号拡張 (ldrsb/ldrsh) され 200→-56 等に化けた。unsigned スカラ・unsigned 配列要素は元から正常。3 経路を修正: (1) local subscript の最終要素判定 `pql==0 && inner_ds==0` が単段ポインタ (pql=1/inner_ds=elem) を最終要素と認識できず → fp の中間行判定と対称な `!is_pointer && !(inner_ds>0 && es>inner_ds)` に。(2) 仮引数 `unsigned char* p` の pointee unsigned を param_decl_spec_t に捕捉し var->is_unsigned へ伝播。(3) `*(p+i)` の ND_ADD/SUB operand を辿る node_pointee_is_unsigned ヘルパ追加。signed char*/short* は符号拡張維持。local/param・deref/subscript/arith・unsigned short・多段/2D を網羅 |
| **グローバルの 2 次元以上のポインタ配列** `int *t[2][2]` / `char *names[2][2]` / `int(*t[2][2])(void)` | 🔧 | global_2d_pointer_array | `t[i][j]` が SIGSEGV (非ポインタ `int t[2][2]` は動作)。3 修正: (1) apply_global_multidim_strides の `!head.is_ptr` ゲートでポインタ要素配列を除外し stride が立たず `t[i]` を「ポインタ値 load→[j] で deref」と誤計算→ゲートを外し elem_size=8 で stride。(2) build_subscript_deref の pointee_is_scalar_ptr が中間次元でも load していた→最終次元 (inner_ds==0) のみ load し中間は伝播 + 要素 pointee サイズを base_deref_size で carry (最終 base は中間 ND_DEREF で gv を引けないため)。(3) 括弧内配列 `(*t[2][2])` の paren_array_mul は積(4)のみで dims を捨てていた→psx_parse_array_suffixes_capture_dims で {2,2} を捕捉。2D/3D データ・char* 文字列・要素代入・2D funcptr (call/値) を網羅 |
| **ローカルの 2 次元以上のデータポインタ配列** `int *t[2][2]` | 🔧 | local_2d_pointer_array | `*t[i][j]` が SIGSEGV (グローバル版は別行、非ポインタ `int t[2][2]` は動作)。register_multidim_array_lvar が outer_stride を立てるが登録後に pql=1/base_deref_size=4 を立てるため、build_subscript_deref の「要素はポインタ」分岐 (pql>=1 && bds>0) が **1 段目** `t[i]` で発火し deref_size を inner_ds(8) から bds(4) に上書き→2 段目が +4/ldrsw (4B) に化けた。fp/unsigned と同じ中間行判定 (inner_ds>0 && es>inner_ds) で 1 段目を中間行と認識し pointer-element 化を最終次元まで遅延・pql/bds を carry。2D/3D・char*・代入を網羅。1D `int *arr[N]`・genuine `int **pp` は不変。**funcptr ローカル `int(*t[2][2])(void)` は別問題 (paren array stride + ネスト brace init) で未修正** |
| `void *` 戻り型を void 関数と誤判定 | 🔧 | void_ptr_return | (整数型の節参照) return チェックが is_pointer 無視 |
| 間接呼び出しの int→fp 引数昇格 (直書き funcptr) | 🔧 | funcptr_int_to_fp_arg | (関数ポインタ節参照) 直書き funcptr のみ。typedef 経由 funcptr は未対応 |
| 可変長引数（int） | ✅ | (probe q1) | |
| 関数ポインタ経由の可変長呼び出し `int(*f)(int,...); f(...)` | 🔧 | variadic_via_func_pointer | 可変長引数がレジスタ渡しされ Apple ARM64 ABI (stack 渡し) 違反で va_arg がゴミ。直接呼び出しは正常。funcptr lvar に is_variadic_funcptr+固定引数数を記録し経由呼び出しでも variadic ABI を選択 |
| volatile スカラ/ポインタの値（`volatile int`, `*p+=`） | ✅ | (probe u3) | 値の正しさのみ。順序保証は別 |
| 文字/エスケープリテラル（`\n` `\0` `\x41` `\101`） | ✅ | (probe u4) | |
| char/short 実引数の int 昇格（可変長経由） | ✅ | (probe u5) | |
| 非可変長の char/short/float 実引数昇格 | ✅ | (probe prom_check) | float→double 昇格含む |
| `restrict` 修飾ポインタ（値の正しさ） | ✅ | (probe restrict_check) | |
| `_Atomic` キーワード（`_Atomic int`/`long` の値） | ✅ | (probe atomic2) | |
| 同梱 `<stdatomic.h>` | 🔧 | stdheader/stdatomic_ops | **本物の LSE アトミックに更新済** (浮動小数の節「本物の <stdatomic.h>」行を参照)。旧・単一スレッド退化版から置き換え、fetch 系は規格通り旧値返却 |
| ワイド文字/文字列リテラル `L'A'` `L"hi"[i]` | ✅ | (probe wide2) | int/long 値として動作 |
| stddef.h の wchar_t / max_align_t（C11 7.19） | 🔧 | stddef_wchar_t, stddef_max_align_t | wchar_t=int(4B), max_align_t=long double(8/8) |
| 戻り値の暗黙変換（int↔double, char/short/unsigned 切り詰め） | ✅ | (probe ret_conv, ret_conv2) | |
| VLA（1D / 2D / 関数引数 / sizeof 全体） | ✅ | (probe vla1, vla2, vla4) | |
| 2D VLA の行の `sizeof(a[0])` / 内側次元が第1パラメータの 2D VLA 引数 | 🔧 | vla_2d_param_and_row_sizeof | A4: `sizeof(a[0])` が要素サイズ→行ストライドスロットを runtime ロード。A5: 内側次元が第1パラメータ (offset 0) の `int a[n][n]` で emit_vla_row_stride_for_params が src_offset==0 を未設定扱いし行ストライド計算を飛ばし SIGSEGV→frame_off 判定に変更 |
| pointer-to-VLA `int (*p)[m]` | 🔧 | pointer_to_vla | ローカルは E3064 で拒否、仮引数 `int (*a)[n]` はコンパイル通過するが行ストライドを実行時 n で計算せずサイレント miscompile していた。行ストライド (extent*elem) を保持する隠しスロット (vla_row_stride_frame_off) をローカル (宣言子で VLA 次元を式捕捉し 16B 確保＋init_chain にストライド store 注入) と仮引数 (既存 register_vla_array_param 機構を再利用、関数 entry で *[rs]=*[n]*elem) の双方で確保。subscript は既存 vla_rsf 経路、ポインタ算術 (+/-) と inc/dec (++/--) はスロットから実行時ストライドを load してスケール。int/double/float/struct 要素・read/write・`(*(p+1))[j]`・p++ 網羅 |

### リンケージ / 複数 TU（extern / static）
複数ファイルをリンクする差分ハーネス（各 .c を ag_c で .s 化→clang で個別アセンブル→
まとめてリンク、clang -I include 直ビルドと exit code 比較）で確認。
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| extern グローバル変数 int/double/struct/`arr[]`/ポインタ の TU 跨ぎ参照 | ✅ | — | 値・型とも clang と一致 |
| extern 関数の TU 跨ぎ呼び出し（int/double 戻り） | ✅ | — | |
| tentative definition `int t;` を複数 TU | ✅ | — | `.comm` で正しくマージ |
| **ファイルスコープ `static` 変数/関数の内部リンケージ** | 🔧 | static_internal_linkage | C11 6.2.2p3 違反。ag_c が `static` に `.global` を出し、無初期化 static を `.comm`（common=外部）にしていたため、同名 static を持つ別 TU と `duplicate symbol` 衝突／共有。codegen で static は `.global` 抑制＋無初期化を `.zerofill __bss`（ローカル）に。グローバル/関数/関数内 static の 3 経路。is_static フラグを global_var_t / ir_func_t / node_func_t に追加し parser から伝播 |
| **extern 宣言＋同一TU定義（tag/typedef 基底）** `extern struct S es; struct S es={7};` | 🔧 | extern_then_def_same_tu | C11 6.9.2。builtin (`extern int v; int v=5;`) は動作。2 つの独立バグ: (1) storage class フラグ (g_*_is_extern/static) が宣言間でリセットされず前の extern が次の bare-struct 定義に漏れ→finalize の extern 分岐が brace を scalar 式として食べ E3064。reset_toplevel_decl_spec_state と parse_toplevel_tag_decl で宣言ごとに 0 クリア。(2) typedef object 経路 (apply_toplevel_typedef_prefix_flags) が extern を無条件 0 にし `extern T et;` が tentative 定義 (.comm) になり本定義 data と重複 ASSEMBLE_FAIL→extern/static を伝播 (漏れは (1) の reset で防止)。static の漏れ (`static struct S a; struct S b;` で b が内部リンケージ化) も解消。逆順 (定義→extern)・別 TU は元から動作。tag/typedef/ポインタ/配列・複数宣言子を網羅 |

---

## バグではない（仕様 / 既知の差異、追わない）
- **ag_c は標準C のみ対象。GNU 拡張は新規に実装しない。差分テストで clang が GNU 拡張として受理するものは「バグ/未対応」として扱わない。**
  - `, ##__VA_ARGS__`（GNU 可変長マクロのカンマ削除）— 未対応のまま。
  - statement expression `({...})`、`typeof`、範囲指定子 `[2 ... 5]=v`、二重波括弧スカラ初期化 — 標準外。
  - （既に入っている GNU 拡張 `__real__`/`__imag__` は complex.h が依存するため残置。新規は入れない。）
- 過剰初期化子 `struct S s={{1,2},{3,4}}`（メンバ1個に2グループ）等は ag_c は意図的に E3064（厳格）。
- `s07.c`（深さ 10 万の再帰）の SIGSEGV はスタックオーバーフロー（誤コンパイルではない）。
- 評価順序が未規定/UB のもの（`a[i++]=i` 等）。

## 未チェックの着手候補（⬜ を埋める）
（現時点で未着手候補なし。下記「既存バグ」も含めすべて対応済み。）

### ストリーミング前方先読み境界バグ（既存バグ・修正済み）
| 領域 | 状態 | fixture / commit | メモ |
|------|------|------------------|------|
| `_Generic` 型照合の `t->next` 先読みがストリーミング窓境界で NULL を踏む | 🔧 | generic_streaming_lookahead | `_Generic` の型照合はカーソルを進めず `t->next` で型全体を先読みする。ストリーミング生成器はカーソル前方を一定窓だけ materialize し、`set_curtok` のジャンプが補充起点 `refill_at` を飛び越えると補充がチェーン末尾到達まで起きない。複雑なグローバル funcptr/ネスト宣言 + 複数の `assert(_Generic)`（`#expr` 文字列化で大量トークン）+ 末尾の cast 制御式の深いネスト型が同居すると、窓境界が型の途中に来て先読みが未生成境界(NULL)を踏み、有効な型を誤却下し「':' が必要 (実際 '(')」E2006。**72b23c2 でも再現する既存バグ**で型シグネチャ照合とは独立。チャンク再利用は無関係(無効化しても再現)。修正: 深い前方先読みの直前に `tk_ensure_lookahead()`（プリプロセッサが登録するフック経由で窓を満たす、parser↔preprocess は疎結合のまま）を呼ぶ |

## チェック済みだが miscompile でなかった領域（再探索不要）
- compound literal: ネスト / `&(compound)` / 配列 / 関数引数 (probe cl1, cl3)。
- `static p = &(struct){..}`（非定数 static init）は clang が拒否、ag_c は受理（寛容な差異）(probe cl2)。
- 浮動小数の `printf` 出力書式・丸め（`%f`/`%g`/`%e`/`%E`、精度・幅・フラグ、`%.0f` の偶数丸め、
  `0.0`/`-0.0`/Inf/NaN、`float`→`double` 可変長昇格、int/float 混在のレジスタ/スタック境界、
  `snprintf`）を stdout 比較で確認。書式・丸めは libc 任せで ag_c は引数を正しく渡しており全て一致。
