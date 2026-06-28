# HANDOFF — ag_c バグ修正セッション

最終更新: 2026-06-29（続き115: Wasm indirect aggregate returns）

## 現状
- `make test` = **green** (tokenizer + parser + preprocess + fuzz + IR + Wasm backend + E2E)。
- 直近確認: `make test` green、`./build/test_wasm32_backend` green、
  `./build/test_e2e` = **1125/1125 green**。
- **c-testsuite**: `bash scripts/run_c_testsuite.sh --list-fail` で 220 件中 **218 pass + 2 unsupported skip**。
- 続き97: **00219** (`_Generic` の array association と関数 designator→function pointer decay)。
- 続き98: 認識済みの未対応 GNU 拡張は `W3024` で「このコンパイラでは使用できない」旨を警告し、
  意味実装せず読み飛ばす。対象: `#pragma push_macro` / `pop_macro`、GNU range designator
  `[lo ... hi] =` (先頭 `lo` の単一 designator として処理)、ゼロ長配列 `[0]` (0 バイトとして処理)。
- 続き99: **明示 `extern int f(...);` 関数内宣言**。通常ローカル prototype は続き77で直っていたが、
  `extern` 付きだけ `parse_local_extern_declarator_list` が関数 declarator を extern 変数として
  登録し、呼び出しが GOT load 経由になって SIGBUS。`extern` 宣言子ループでも関数 suffix を検出し、
  non-pointer 関数 prototype は変数登録せず読み飛ばす。
- 続き100: **関数内 `extern struct/union T obj;`**。stmt.c の tag-keyword fast path が
  `extern` プレフィックスを消費するだけで storage class を復元せず、`struct S gs;` と同じ
  auto 変数として登録して未初期化スタックを読んでいた。tag 経路で `extern` を保存/復元し、
  `psx_decl_parse_declaration_after_type_ex` でも extern なら local extern 登録へ回す。
  local extern 登録には tag/fp/unsigned 情報も持たせる。
- 続き101: **c-testsuite 残 2 件を unsupported GNU skip として明示**。00206
  (`#pragma push_macro` / `pop_macro`) と 00216 (空 struct / GNU range designator) は
  方針どおり意味サポートせず、harness 側で `Skip unsupported: 2` として fail 集計から除外。
- 続き102: **`const struct` / `const struct *` のメンバ代入拒否**。stmt.c の tag-keyword
  fast path が `const struct S s` / `struct S const s` / inline tag の `const` を
  after_type に渡さず、さらに `s.x` の ND_DEREF 代入で const を見ていなかったため
  `s.x = ...` が通っていた。tag 経路で const/volatile を保存し、メンバ deref に親 const を伝播、
  const 付き ND_DEREF への代入を E3077 にする。
- 続き103: **`const struct` 配列要素のメンバ代入拒否**。`const struct S a[1]; a[0].x = ...`
  が、配列 decay の `ND_ADDR` と subscript 結果 `ND_DEREF` に const/volatile が伝播せず通っていた。
  ローカル/static local/global 配列の base address node と global_var_t に qualifier を保持し、
  subscript 結果へ伝播して E3077 にする。
- 続き104: **`const struct S (*p)[N]` の deref/subscript 経由メンバ代入拒否**。
  `const struct S (*p)[1]; (*p)[0].x = ...` で単項 `*` の結果に pointee const/volatile が
  伝播せず、後続 subscript/member 代入が通っていた。単項 deref で operand の qualifier を
  結果 `ND_DEREF` に伝播し E3077 にする。
- 続き105: **関数が返す const pointee 経由のメンバ代入拒否**。
  `const struct S *get(void); get()->x = ...` が tag 戻り型 parsing で implicit int に落ち、
  pointer-to-array 版 `const struct S (*get(void))[1]; (*get())[0].x = ...` も関数戻り値の
  pointee const が式ノードへ伝播せず通る穴があった。tag 前の const/volatile を関数戻り型として
  consume し、関数 semantic ctx に ret_pointee qualifier を保存、`->` / `*` / `[]` の
  ND_FUNCALL 経路で復元して E3077 にする。
- 続き106: **関数ポインタが返す const pointee 経由のメンバ代入拒否**。
  `const struct S *(*fp)(void); fp()->x = ...` が、間接 `ND_FUNCALL` では callee の関数ポインタ型に
  ある pointee const を見ずに通っていた。さらに `const struct S (*(*fp)(void))[1]; (*fp())[0].x`
  は `*fp()` の結果に戻り tag が伝播せず E3005 になっていた。間接呼び出しでは callee の
  `node_mem_t` から const/volatile と戻り tag を復元し、読み取りは許可、書き込みは E3077 にする。
- 続き107: **typedef 関数ポインタ経由呼び出しの int→float/double 引数変換**。
  `typedef double (*Op)(double); Op op; op(3)` が、直書き関数ポインタと違って typedef に
  仮引数 fp マスクを保存しておらず、整数実引数を x0/w0 に置いたまま間接呼び出ししていた。
  `psx_typedef_info_t` と local/global 関数ポインタ変数に `funcptr_param_fp_mask` を伝播し、
  `ND_LVAR` / `ND_GVAR` の間接呼び出しで既存の `wrap_to_fp` に乗せる。直前 typedef の
  stale mask が別 typedef 変数へ漏れないよう、直書き宣言子だけ `psx_last...` を優先する。
- 続き108: **struct メンバ関数ポインタ経由呼び出しの int→float/double 引数変換**。
  `struct Ops { double (*f)(double); }; ops.f(3)` では callee が `ND_DEREF` になり、
  local/global funcptr 用の仮引数 fp マスクを参照できず、整数実引数を x0/w0 に置いたままだった。
  さらに brace 初期化 `struct Ops ops = { add_half };` では、メンバ `fp_kind` を関数戻り型ではなく
  メンバ自身の FP 型として扱い、関数アドレスを double 化して格納していた。tag member と
  `node_mem_t` に `funcptr_param_fp_mask` を伝播し、`ND_DEREF` callee でも `wrap_to_fp` を適用。
  初期化時は tag pointer メンバの `fp_kind` を FP store に使わないようにした。
- 続き109: **関数ポインタ経由呼び出しの float/double 実引数→整数仮引数変換**。
  `int (*fp)(int); fp(7.9)` が、直接呼び出し用の `param_int_sizes` に乗らず、FP 実引数を
  d0/s0 に置いたまま callee が x0/w0 を読んでいた。関数ポインタ型にも
  `funcptr_param_int_mask` (1=4B, 2=8B) を保存し、typedef/local/global/tag member/`node_mem_t`
  へ伝播、`ND_LVAR` / `ND_GVAR` / `ND_DEREF` callee で FP 実引数を `ND_FP_TO_INT` にラップする。
  `ND_FP_TO_INT` は long 仮引数用に `type_size==8` なら i64 F2I を返すようにした。
- 続き110: **関数ポインタが配列へのポインタを返す間接呼び出し**。
  `int (*(*fp)(void))[3]; fp()[1][2]` が、直接関数版と違って callee 側に戻り値の
  pointee 配列次元/要素サイズを持っておらず E3064 または誤スケール。`double (*(*fp)())[2]`
  では callee の `pointee_fp_kind=double` を「関数戻り値 double」と扱い、実際はポインタ戻りなのに
  d0 から読んで SIGSEGV。関数ポインタ型に
  `funcptr_ret_pointee_array_first_dim` / `funcptr_ret_pointee_array_elem_size` を保存し、
  typedef/local/global/tag member/`node_mem_t` へ伝播。間接 `ND_FUNCALL` の deref_size/subscript/
  `*fp()` 経路で使用し、pointer-to-array 戻りでは fp_kind を funcall 戻り値に立てず要素 fp として
  subscript に渡す。
- 続き111: **直接関数が 2D 配列へのポインタを返す呼び出し**。
  `int (*get(void))[3][4]; get()[1][2][3]` が、続き19 の直接関数版で先頭次元 N だけを記録していたため、
  2D pointee の stride が N*elem になり、実際に必要な N*M*elem / M*elem / elem を carry できず
  SIGSEGV。関数 semantic ctx に第2次元 M も保存し、直接 `ND_FUNCALL` の deref_size/subscript/
  `*get()` 経路へ伝播。int/double、read/write、`(*get())[j][k]`、`(*(get()+i))[j][k]`、引数ありを網羅。
- 続き112: **関数ポインタが 2D 配列へのポインタを返す間接呼び出し**。
  `int (*(*fp)(void))[3][4]; fp()[1][2][3]` が、続き110 の 1D 関数ポインタ版で
  first_dim/elem_size だけを保存していたため、2D pointee の stride が N*elem になって SIGSEGV。
  `funcptr_ret_pointee_array_second_dim` を typedef/local/global/tag member/node_mem_t へ伝播し、
  間接 `ND_FUNCALL` の deref_size/subscript/`*fp()` 経路で N*M*elem / M*elem / elem を carry。
  直書き global/struct member では trailing `[N][M]` をオブジェクト自身の配列ではなく戻り
  pointee 次元として登録する。
- 続き113: **IR ベース Wasm backend の拡張**。ARM64 backend は維持しつつ `build/ag_c_wasm`
  の WAT 出力を段階的に拡張。主なコミット:
  - `0037e7c` simple indirect call、`e41f7f0` function pointer initializer、
    `f1c60ea` void / unused-result indirect call。
  - `721ef89` global function pointer tracking、`51c0976` struct member function pointer call、
    `54c4c92` struct member function pointer array。
  - `cb90307` simple indirect pointer return、`4b38be5` zeroed large Wasm globals、
    `aadc081` zeroed aggregate globals の fixture。
  対応済み範囲: local/global/static/struct member の単純な関数ポインタ呼び出し、関数ポインタ配列、
  void / unused-result call、int↔fp 引数変換が IR に現れる indirect call、単純 pointer return、
  pointer-to-array return、未初期化の大きい global/aggregate を Wasm linear memory のゼロ初期化に任せる処理。
  制御フロー越しに global/struct member 関数ポインタが上書きされる場合は、誤コンパイルせず E4008 で止める。
  WAT は `wat2wasm` / `wasm-interp` がある環境では test harness が実行値まで確認する。
- 続き114: **多次元 static local 整数配列の lowering**。
  `int f(){static int a[2][3]; a[1][2]++; return a[1][2];}` が、1D static local 配列の
  lowering 対象から外れて auto 多次元配列として登録され、呼び出し間で永続化せず ARM64/Wasm とも
  stack frame (`alloca`) に置かれていた。`try_lower_static_local_array` が `[` 直後の多次元
  定数 suffix を peek/consume して `try_lower_static_local_array_consumed` へ渡すよう拡張し、
  lowering 先 `global_var_t` と alias `lvar_t` に `outer_stride` / `mid_stride` /
  `extra_strides` を保存。`build_static_local_array_addr_node` でその stride を
  `ND_ADDR(ND_GVAR)` に伝播する。2D/3D、初期化あり/なし、永続性、`sizeof` を fixture 化し、
  Wasm backend でも実行値 12 を確認する。
- 続き115: **Wasm indirect aggregate return**。
  `struct Big (*fp)(int); fp(40)` のような 1/2/4/8B に収まらない struct 値返しの関数ポインタ
  indirect call が、Wasm backend で E4008 になっていた。IR はすでに `ret_struct_area` を持つため、
  `call_indirect` の typeuse に hidden return area `i32` を先頭 param として追加し、実引数列の前に
  `ret_struct_area` を渡す。aggregate return は Wasm 上は result なしとして扱う。local/global/struct
  member 関数ポインタ、変数への受け取り、直接メンバ materialize (`fp(1).b`) を
  `test_wasm32_backend` で WAT と `wasm-interp` 実行値まで確認する。

### Wasm backend の既知メモ

- Wasm indirect aggregate return (`ret_struct_size > 0`) は local/global/struct member 関数ポインタで対応済み。
  制御フロー越しに global/struct member 関数ポインタが上書きされる場合は、引き続き誤コンパイルせず E4008。
- 大きい未初期化 global は data segment を出さず、`data_addr_for_global` によるアドレス予約だけ行う。
  initialized な大きい object は既存の aggregate/array 初期化経路に従う。

## 次セッション開始時の手順
1. **HANDOFF.md を読む** (このファイル)。「現状」「次セッションの最優先タスク」「作業のやり方」を確認。
2. **`git submodule update --init`** で c-testsuite を初期化 (未取得時のみ)。
3. **`make test`** で green を確認 (前回セッションの状態が引き継がれている)。
4. **`bash scripts/run_c_testsuite.sh --list-fail`** で fail 0 / unsupported skip 2 を確認 (= 前回セッションのベースライン)。
5. **bug_coverage.md** で再探索不要な領域を確認 (重複探索を避ける)。
6. **次セッションの最優先タスク** (下記) のうち 1 件を選んで取り組む。または未探索の角度から
   probe (`/tmp/*.c`) を作り `scripts/agc_diff_test.sh` で差分テスト。
7. **新規バグを発見** したら、HANDOFF と同じ流れ (修正 → fixture 登録 → コミット) で進める。

## 次セッションの最優先タスク

### A. c-testsuite の残失敗から修正 (推奨、進捗測りやすい)

`make c-testsuite-verbose` で失敗一覧を見て、未着手の残件を順次修正していく。
B1 軽量・B2 の **00121/…/00214** は **続き82-91 で完了**。**00089** は **続き92**、**00129** は **続き93**、**00200** は **続き94**、**00204** は **続き95**、**00205** は **続き96**、**00219** は **続き97 で完了**。c-testsuite の残件は GNU 拡張として harness で unsupported skip 扱い。

#### 取り組み順 (軽量 → 中規模 → 大規模)

**B1. 軽量 (修正範囲が局所、影響度小)** — 完了
- **00145**: ✅ 続き79 (`pp_if_short_circuit`)
- **00152**: ✅ 続き80 (`pp_line_macro_arg`)
- **00212**: ✅ 続き81 (`pp_predefined_lp64`)

**B2. 中規模 (parser / 型システム)**
- **00121**: ✅ 続き82 (`mixed_decl_func_proto_and_var`) — カンマ区切り toplevel 宣言を
  `funcdef()` ではなく declaration 経路へ。prototype 登録 + 変数 `a` 登録。
- **00124**: ✅ 続き83 (`func_returning_funcptr_call`) — pointer-to-function (戻り funcptr)
  の pql 補正 + `(*call())(args)` deref 減衰。
- **00151**: ✅ 続き84 (`global_incomplete_outer_array_dim`) — `int arr[][3][5]` の
  行境界揃え + 外側次元推論。
- **00189**: ✅ 続き85 (`global_variadic_funcptr_call`) — グローバル可変長 funcptr の
  `...` 解析 + ND_GVAR 経由呼び出しで variadic ABI。
- **00201**: ✅ 続き86 (`macro_nested_paste_call`) — `CAT(A,B)(x)` の `)(` splice +
  hideset 伝播修正。
- **00202**: ✅ 続き87 (`macro_paste_empty_operand`) — 空引数 placemarker と `##`。
- **00209**: ✅ 続き88 (`incomplete_tag_and_nested_func_param`) — 未完了タグ前方宣言 +
  `enum E const *` + `int (int x)` 仮引数。
- **00210**: ✅ 続き89 (`gnu_attribute_parse`) — `__attribute__((...))` を宣言・関数・
  キャストで読み飛ばし。
- **00213**: ✅ 続き90 (`gnu_statement_expression`) — `({ ...; expr })` を
  ND_STMT_EXPR でパース・コード生成。
- **00214**: ✅ 続き91 (`builtin_expect_fold`) — `__builtin_expect(exp, c)` を
  exp に畳み込み (外部シンボル参照を出さない)。

**B3. 大規模 (重い、影響範囲広い)**
- **00089**: ✅ 続き92 (`func_returning_funcptr_chain`) — `go()()->zerofunc()` の
  2 段目 funcall 戻り tag 伝播 + typedef 関数ポインタ戻り型の記録。
- **00129**: ✅ 続き93 (`typedef_label_shadow`) — typedef 名 `s` と同名ラベル `s:` を
  宣言より先にラベルとして解釈。
- **00200**: ✅ 続き94 (`shift_left_operand_type`) — シフト結果型を promoted left operand に。
  併せて `(long)` cast の型幅保持、stream cast 先読み補充、長大マクロ展開で露出した大 frame
  spill の `ldr/str [x29,#off]` 即値範囲超を修正。
- **00204**: ✅ 続き95 (`arm64_aggregate_varargs`) — ARM64 aggregate ABI の
  struct 値渡し/戻り、グローバル aggregate、長さ 1 配列メンバ、variadic aggregate stack slot。
- **00205**: ✅ 続き96 (`global_struct_array_flat_elision`) — J interpreter snippet の
  `PT cases[] = { scalar... }`。グローバル struct 配列の flat brace elision で scalar 後に
  毎回次要素境界へ揃えていたためメンバが 0 化し、さらに未完了 `[]` の型サイズ推論が
  flat slot 数を struct 要素数として扱って余分な 0 要素を出力していた。
- **00219**: ✅ 続き97 (`generic_array_assoc_and_func_designator`) — `_Generic` の
  `int[4]` association を scalar `int` と誤マッチさせず、関数 designator `foo` を
  function pointer へ decay して typedef 関数ポインタ association に一致させる。

#### 対象外 (GNU 拡張、HANDOFF ルールで skip)
- **00206**: `#pragma push_macro` / `pop_macro` (GCC/MSVC 拡張)
- **00216**: 空 struct `typedef struct {} empty_s;` (GCC 拡張)

### B. 未探索の角度から新規バグ探索 (探索路線)

候補:
- libc 関数連携の更に深い (snprintf format flags、qsort 複雑な comparator、stdlib chain、math 連鎖)。
- ランダム生成ファズ (深いネスト・複合的なアルゴリズム・大きいプログラム)。
- 複合代入の細形 (struct メンバ + 多次元 + ポインタ deref 組合せ)。
- 宣言子の特殊な組合せ (typedef chain × paren-array × funcptr の更なる組合せ)。
- 複数 TU リンク (`test_e2e.c` の `link2_cases[]` 経由、または使い捨ての `/tmp/*.sh` で
  クロス TU 比較; static_internal_linkage_xtu_{main,other} を参考に)。
- 古い C コードの寛容性 (K&R, implicit int 等; ただし GNU 拡張は対象外)。

### C. 既知の follow-up (今セッションで触れて残ったもの)

- 現時点で明示的な follow-up なし。

## 重要な約束事 (memory より)
- **1 タスクずつ進める**: 完了後にユーザー確認を取ってから次へ。複数タスクを並行しない。
- **コミットまでがタスク**: タスク完了時はコミットも自分で実行 (`feedback_commit_per_task.md`)。
  「コミットしますか？」と毎回聞かない。
- **作業前に範囲確認**: 狭い依頼を勝手に全体へ広げない (`feedback_confirm_scope_before_acting.md`)。
- **GNU 拡張は対象外**: ag_c は C11 サブセット。新規に意味サポートしない。認識済みの未対応拡張は
  `W3024` で警告して読み飛ばすが、clang/GCC 互換の挙動は保証しない (`feedback_no_gnu_extensions.md`)。
- **コミットメッセージ**: 英語 Conventional Commits (`fix:` / `docs:` 等)。Co-Authored-By を付ける。
- **テスト出力を省略しない**: `make test` の結果はそのまま出す (`feedback_trust.md`)。
- **ヘッダ変更時は `make clean && make`** で確認 (増分ビルドが依存を取りこぼすことがある)。

## ag_c の基本情報
- **ターゲット**: Apple Silicon ARM64 (Mach-O)。クロスは未対応。
- **言語**: C11 サブセット。GNU 拡張なし。
- **コンパイル**: `./build/ag_c foo.c > foo.s` (アセンブリを stdout)。`-o`/`-I` フラグなし。
  include 検索は CWD 相対の `include/`。
- **ビルド**: `make` (日本語診断 `-DDIAG_LANG_JA`)。
- **テスト**: `make test` (E2E + parser/preprocess 単体 + fuzz + IR)。
- **差分テスト**: `scripts/agc_diff_test.sh <file.c>` で agc と clang を比較
  (exit code/stdout/stderr の 3 つを照合)。詳細は下記「作業のやり方」。
- **アーキ流れ**: tokenizer → preprocess → parser → IR builder → ARM64 codegen。

## このセッション（続き70-78）累計成果: 9 件の修正 + c-testsuite 組み込み

| # | 続き | コミット | 内容 |
|---|---|---|---|
| 1 | 70 | `2801eec` | struct メンバ `int (*p)[N]` (pointer to array) が `int *p[N]` と区別されず sizeof/access が誤動作 |
| 2 | 71 | `e5ed9b8` | struct メンバ `int (*p[M])[N]` (array of pointer to array) sizeof/access |
| 3 | 72 | `52771c4` | struct メンバ `int (*p)[M][N]` (2D pointee の pointer-to-array) — pointee dim 情報が落ちて誤スケール |
| 4 | 73 | `37a502d` | グローバル plain 多次元配列の `[N]={[M]=V}` designator が単一スカラ scale で誤ジャンプ |
| 5 | 74 | `c19af41` | グローバル struct メンバ 2D struct タグ配列の外側 `[N]=` designator で内側次元が無視される |
| 6 | 75 | `140070d` | グローバル struct メンバ多次元 struct タグ配列の内側 brace 内 designator (`.member=` / `[M]=`) が E3064 |
| 7 | 76 | `96115fd` | ローカル struct メンバ多次元 struct タグ配列の designator init が parser エラー |
| 8 | 77 | `74b8e0d` | 関数内ローカル関数 prototype (`int f1(char *);`) がローカル変数化されて SIGSEGV (c-testsuite 00078) |
| 9 | 78 | `dd7c614` | `sizeof((int) 1)` のような cast 式に対する sizeof が E2006 (c-testsuite 00155) |

### 修正の主な領域
- **struct_layout.c**: pointer-to-array メンバ系の 4 修正 (続き70-72, 74) — `ptr_in_paren` /
  `ptr_array_pointee_bytes` フィールド追加、pointee dims を outer_stride/mid_stride に反映、
  struct タグ配列メンバの arr_dims 保存条件緩和。
- **parser.c**: トップレベル多次元配列の sub_dims 算出 (続き73)、gbrace_child_at の tag_kind
  非依存化と TK_STRUCT branch の sub_dims 積算 (続き74-75)。
- **decl.c**: parse_member_initializer の outer_stride 経路に designator + struct 要素対応
  (続き76)、関数 declarator の登録 skip (続き77)。
- **expr.c**: build_member_deref_node / build_unary_deref_node / build_subscript_deref に
  pointer-to-array carry 機構 (続き70-72)、parse_parenthesized_type_size の cast 式巻き戻し
  (続き78)。
- **ast.h / semantic_ctx.{h,c}**: 新フィールド `ptr_array_pointee_bytes` 追加 (続き71)。

### 全 fixture リスト (本セッション分)
- struct_ptr_to_array_member.c (続き70)
- struct_array_of_ptr_to_array_member.c (続き71)
- struct_ptr_to_2d_array_member.c (続き72)
- global_multidim_array_nested_designator_plain.c (続き73)
- global_struct_member_multidim_struct_array_designator.c (続き74)
- global_struct_member_multidim_nested_designator.c (続き75)
- local_struct_member_multidim_nested_designator.c (続き76)
- local_function_prototype.c (続き77)
- sizeof_cast_expression.c (続き78)

## c-testsuite 組み込み (今セッション)

- **submodule**: `test/external/c-testsuite/` (https://github.com/c-testsuite/c-testsuite, MIT)
- **harness**: `scripts/run_c_testsuite.sh` — 各 `tests/single-exec/NNNNN.c` を ag_c で compile →
  `cc -arch arm64` で link → 実行 → exit code & stdout を `.expected` と比較
- **Makefile targets**:
  - `make c-testsuite` — pass/fail サマリ
  - `make c-testsuite-verbose` — 各カテゴリ先頭 20 件の失敗一覧
  - `bash scripts/run_c_testsuite.sh --list-fail` — 全失敗 ID 列挙
- **設計判断**: `make test` には含めない (失敗テスト多数のため別 target)。`make test` は引き続き
  100% green を維持する。

### c-testsuite 現状 (続き101 後): 218 pass + 2 unsupported skip

```
Total:           220
Pass:            218
Skip unsupported: 2
Fail (compile):  0
Fail (assemble): 0
Fail (runtime):  0
Fail (stdout):   0
Pass率:          99.1%
対象Pass率:      100.0%
```

### unsupported skip テスト分類 (2 件、どちらも GNU 拡張)

**Unsupported GNU extension skip (2 件)**:
- 00206 (`#pragma push_macro` / `pop_macro`)
- 00216 (空 struct `typedef struct {} empty_s;`、GNU range designator など)

実質取り組み対象は **0 件**。続き98 で認識済み GNU 拡張の一部は `W3024` 警告 + 読み飛ばしに
したが、GNU 拡張の意味サポートはしない方針のまま。続き101 で c-testsuite harness に
unsupported skip を明示し、残失敗は 0 件になった。次は未探索の角度から probe を作るか、
GNU 拡張サポートを方針変更として明示的に扱う場合のみ 00206/00216 に取り組む。

## 前セッション（続き56-69）累計成果: 14 件の miscompile / parse error 修正

差分テスト (`scripts/agc_diff_test.sh`) sweep で発見した miscompile / parse error を順次修正。
詳細はコミット履歴と各 fixture (`test/fixtures/probes_found_bugs/`) のヘッダコメント参照。

| # | 続き | コミット | 内容 |
|---|---|---|---|
| 1 | 56 | `d22bd1e` | `_Bool b:1` bitfield 読み出しが -1 (符号拡張誤り) → 0/1 |
| 2 | 57 | `58d6fe3` | 匿名 struct/union 内 bitfield 昇格時に bit_width が落ちて full-width load |
| 3 | 58 | `789690a` | ポインタ typedef を struct メンバに使うと 4 バイト store → SIGSEGV |
| 4 | 59 | `4cf62ab` | 配列 typedef を struct メンバ初期化で E3064 |
| 5 | 60 | `948e3c0` | static-local array alias が param と同 offset で alloca 衝突 → SIGSEGV |
| 6 | 61 | `624f91e` | static-local 文字列ポインタ 3 件 (subscript 不可 / NULL 化け / 負値リテラル) |
| 7 | 62 | `61cf2da` | `typedef T *X[N]` (array of pointers) を単一ポインタと誤認 |
| 8 | 63 | `077cbd2` | typedef array dims と declarator 側 [N] の連結 (struct メンバ) |
| 9 | 64 | `15af560` | `(char*)&struct_var - (char*)&struct_var.m` 形が init で reject |
| 10 | 65 | `3f9f2b2` | ネスト struct のアラインメントが sizeof と混同 (`{struct Inner i; int}` が 16 に化ける) |
| 11 | 66 | `4b75452` | `const char *p = "..." + N;` グローバル init が `.comm` に落ちて NULL |
| 12 | 67 | `7efbfc6` | 同上の配列/struct 要素版 (`{"abc"+2, ...}`) |
| 13 | 68 | `4795bf4` | `long g = &arr[i] - &arr[j];` グローバル init が定数畳み込み失敗 |
| 14 | 69 | `e3b8053` | `int g = (int)3.7;` グローバル init が ND_FP_TO_INT のまま `.comm` に化ける |

### 修正の主な領域
- **struct_layout.c**: bitfield 符号性 (続き56)、匿名昇格 bitfield 属性保持 (続き57)、ポインタ typedef
  メンバ幅 (続き58)、配列 typedef メンバ次元連結 (続き59 + 63)、ネスト struct アラインメント (続き65)。
- **ir_builder.c**: static-local alias を find_owning_lvar / alloca-prepass の両方で skip (続き60、61)。
- **decl.c / expr.c**: static-local-scalar の init peek / 文字列ポインタ subscript (続き61)、
  `*X[N]` array-of-pointers typedef (続き62)、`(char*)&struct` cast (続き64)。
- **parser.c**: グローバル init の文字列+offset (続き66)、resolve_global_addr_init の公開化と
  ptrdiff fold (続き68)。
- **arm64_apple.c**: 文字列 sentinel + offset の emit (続き66、67)。
- **expr.c**: 浮動 ND_NUM → 整数キャスト upfront fold (続き69)。

### 全 fixture リスト
- bool_bitfield.c
- anon_struct_bitfield_promote.c
- struct_pointer_typedef_member.c
- struct_array_typedef_member.c
- static_local_array_param_overlap.c
- static_local_string_pointer.c
- typedef_array_of_pointers.c
- struct_array_typedef_member_2d.c
- struct_addr_cast_subtract.c
- struct_member_alignment.c
- global_string_offset_init.c
- global_string_offset_in_array_and_struct.c
- global_ptrdiff_init.c
- global_int_from_float_cast.c

## このセッション（続き45）: 代入を条件 / 整数オーバーフロー / dangling pointer
3 件の W3001 warning を追加:
- `if (x = 10)` / `while (x = 0)` — 代入を条件として使う (clang -Wparentheses 相当)。
  parse_stmt_if / parse_stmt_while で条件式 top が ND_ASSIGN なら警告。
- `char c = 300;` / `short s = 70000;` — 整数リテラル範囲外 (clang -Wconstant-conversion 相当)。
  decl.c のスカラ初期化分岐で var->elem_size < 4 かつ ND_NUM の値が型範囲外なら警告。
  `unsigned char uc = -1;` は全ビット 1 のイディオムとして除外。
- `return &x;` — ローカル変数アドレスを返す dangling pointer (clang -Wreturn-stack-address
  相当)。parse_stmt_return で ND_ADDR(ND_LVAR) かつ非 static なら警告。

`make test`=1053/1053 green。

## このセッション（続き44）: 縮小変換と自己比較の警告
- 整数変数を浮動小数点リテラルで初期化 `int x = 1.5;` (clang -Wliteral-conversion 相当)。
  decl.c のスカラ初期化分岐で ND_NUM の fval に小数部があれば W3001 warning。
- 自己比較 `x == x` / `x != x` (clang -Wtautological-compare 相当)。equality() で両辺が
  同じ ND_LVAR offset または同じ ND_GVAR 名なら警告。
- while/for の空本体は意図的な busy wait の慣用句で clang も警告しないため未追加。

`make test`=1052/1052 green。

## このセッション（続き43）: コンパイル時 UB / 怪しい書き方の警告 4 件
コンパイル時に検出可能な未定義動作・タイプミスの警告を追加:
- 0 リテラルでの除算・剰余 `1 / 0` / `1 % 0` (C11 6.5.5p5)
- シフト量が型の幅以上 / 負 `1 << 32` (C11 6.5.7p3)
- 自己代入 `x = x` (clang -Wself-assign 相当)
- 空 if 本体 `if (cond);` (clang -Wempty-body 相当)

実装: mul() / shift() / assign() / parse_stmt_if() で diag_warn_tokf を呼ぶ。
合法形 (非ゼロ除算、適切なシフト、別変数代入、本体ありの if) を fixture で網羅。

`make test`=1051/1051 green。

## このセッション（続き42）: タグ再定義 + 非 void 関数 return なし
- **タグ再定義** (struct / enum): `struct S{int x;}; struct S{int y;};` の重複定義が silently
  通過していた。psx_ctx_define_tag_type_with_layout で同一スコープに既存の完全型
  (member_count > 0) があり今回も完全型ならエラー。前方宣言 → 定義は従来挙動。
- **非 void 関数の return なし** (C11 6.9.1p12): emit_implicit_return_if_missing で main
  以外の非 void 関数で値を返さず終端していたら W3001 warning。
- 副次: ps_program_from の冒頭で psx_ctx_reset_tag_diag_state / reset_function_diag_state
  を呼び出すソフトリセットを追加。ユニットテスト用 (実コンパイル 1 ファイル 1 プロセスなので
  影響なし)。

`make test`=1050/1050 green。

## このセッション（続き41）: 追加識別子診断 — 関数代入 / enum 衝突 / 暗黙関数宣言
ユーザー指示「順番に」を受けて続き40 の延長として 3 件:
- 関数識別子への代入 `f = 5;` が "ir build/emit failed" 粗エラーで止まっていた。assign 関数で
  ND_FUNCREF を check し E3064 「関数識別子に代入することはできません」を発する。
- enum 定数と通常 identifier の名前空間衝突 (`enum E{A=5}; int A=10;` / 逆順) が見逃されて
  いた。register_toplevel_global_decl で psx_ctx_find_enum_const、enum_const.c で
  psx_find_global_var / psx_ctx_has_function_name / psx_ctx_find_typedef_name を双方向 check。
- C99/C11 で禁止の implicit function declaration `undecl_func()` が silently 通過していた。
  build_unqualified_call で psx_ctx_has_function_name と psx_find_global_var に見つからない
  場合は W3001 warning を出す (clang は default で warning、`-Werror=implicit-function-declaration`
  で error)。

`make test`=1049/1049 green。

## このセッション（続き40）: 識別子の名前空間衝突検出
ユーザー指示「37 から順に」を受けて続き37〜39 の延長として 4 件を検証・修正:
- `extern int g; double g = 1.5;` (extern と定義の型不一致): silently 通過していた。
  register_toplevel_global_decl の merge ロジックを extern も含めて一本化し、型互換チェックを
  両方向に適用。
- `int foo(int){...} int foo;` (関数→変数): register_toplevel_global_decl で
  psx_ctx_has_function_name を確認、当たれば E3064。
- `int bar; int bar(int){...}` (変数→関数): funcdef の本体パース直前で find_global_var_by_name
  を確認、当たれば E3064。
- `typedef int T; int T = 5;` (typedef→変数): register_toplevel_global_decl で
  psx_ctx_find_typedef_name を確認、当たれば E3064。

`make test`=1048/1048 green。

## このセッション（続き39）: declaration-specifier 順序自由 + storage class 重複 + グローバル重複定義
ユーザーの問題提起「同じ名前の変数のチェックと、static/const/volatile を複数同じものを書く、
違う順序で書く、誤った組み合わせで書く」を契機に検証。3 件の関連バグを発見・修正:

1. **順序自由** (C11 6.7p1): `int static x = 5` のように「型 → storage class 順」が E3016
   で拒否されていた。psx_consume_type_kind のループに「型指定子後の storage class / qualifier
   を消費し flag を立てる」分岐を追加。`const static int`、`int static const`、
   `unsigned static int` 等を許容。
2. **storage class 重複検出** (C11 6.7.1p2): `int static static x`、`static int static`、
   `static int extern` 等の interleaved 重複/併用が見逃されていた (skip_cv_qualifiers の
   storage_count は単発呼び出し内のみ)。上記ループ内分岐で g_last_decl_is_static /
   is_extern を見て 2 度目で E3064。
3. **グローバル変数の重複定義** (C11 6.9.2 / 6.7p4): `int g=1; int g=2;` の重複定義や
   `int g; double g;` の型違いが silently 通過していた (後段でアセンブラの duplicate symbol
   で気づくのみ)。register_toplevel_global_decl で同名既存を merge (型互換チェック付き)、
   apply_toplevel_object_initializer で `=` 消費時に既存 has_init を検出して E3064。
   tentative def 同型 (`int g; int g;`) は合法 merge。

副次: ps_program_from に「既存 global var の has_init をクリア」を追加。同一プロセス内で
複数回 ps_program_from を呼ぶユニットテストで前回パースの has_init が残らないように
(実コンパイルは 1 ファイル 1 プロセスなので影響なし)。

`make test`=1047/1047 green。

## このセッション（続き38）: 関数の重複定義検出
ユーザーの問題提起「同じ名前の関数が宣言、定義されている場合は検査されているか」を契機に検証。
重複定義 `int f(){...} int f(){...}` が silently 通過し、後段でアセンブラ/リンカが duplicate
symbol を出すまで気づけなかった。

修正 (C11 6.9p3):
- func_name_t に is_defined フラグを追加。
- psx_ctx_track_function_defined を新設。初回は立てて 1、立っていれば 0。
- funcdef で proto `;` を弾いた後 (本体パース直前) でこれを呼び、0 なら E3064。
- プロトタイプ宣言 (`;` で終わる) は何度書いても合法 (フラグは立たない)。
- proto + def 混在 / static / 複数 proto + 1 つの定義の合法形を fixture で網羅。

`make test`=1046/1046 green。

## このセッション（続き37）: 関数宣言/定義シグネチャ照合
ユーザーの問題提起「同じ関数の宣言と定義で形が違う場合は検査されているか」を契機に検証。
戻り型のみ既存照合 (psx_ctx_track_function_ret_type) で、引数数 / 引数型の不一致は素通しだった:
- `int g(int); int g(int x, int y) {...}` (引数数違い) — silently 通過
- `int h(int); int h(double x) {...}`     (引数型違い) — silently 通過

修正 (C11 6.7p4):
- psx_ctx_track_function_nargs: 初回登録/以降比較で引数数 + 可変長性を照合。
- psx_ctx_track_function_param_category: 各引数を粗粒度カテゴリ (INT/FLOAT/DOUBLE/PTR/STRUCT)
  で照合。funcdef の param 走査内で track し、不一致なら E3064。
- 整数の幅 (4 vs 8) は宣言と定義で粒度を変えても等価扱い (proto の placeholder ND_NUM は
  sz=4、def の ND_LVAR は abi_type_size=8 で本来一致しないため、INT は 1 カテゴリに集約)。
  long vs int の厳密区別は後続課題。
- fixture function_redecl_signature で合法な再宣言が false-positive で弾かれないことを確認。

`make test`=1045/1045 green。

## このセッション（続き36）: 探索 round で 2 件発見・修正
続き35 の探索後、新たな probe round で 2 件発見・修正:
- **`struct N **` 仮引数の `(*root)->m`**（struct_pp_param_arrow）。多段の struct ポインタ仮引数
  で `(*root)->m` が E3005 で弾かれていた (ローカル `struct N **root` は動作)。
  register_param_lvar の struct ポインタ分岐 (param_ptr_levels>=2) で pointer_qual_levels が
  立っておらず、build_unary_deref_node の `*root` で is_tag_pointer 伝播が pql>=2 を要求して
  0 にクリアされ、続く `->` が base_is_ptr=0 で弾かれていた。
- **ファイルスコープの `T *p = (T[]){...}`**（file_scope_ptr_from_array_compound）。ポインタ
  変数を配列複合リテラルで初期化すると SIGBUS。apply_toplevel_object_initializer の strip
  heuristic が `(T){...}` を無条件で剥がして `T *p = {...}` (複数値で初期化) と解釈し、先頭
  要素値がポインタスロットに書き込まれていた。修正: 集約 (配列 / struct 値 / union 値) の
  ときだけ strip。ただし `char *p = (char[N]){"str"}` の単一文字列形は等価なので peek で例外
  許可。`make test`=1044/1044 green。

## このセッション（続き35）: 局所 VLA tag carry + extern global GOT
3D+ VLA 仮引数完了後の探索 round で 2 件発見・修正:
- **局所 VLA のタグ carry**（vla_struct_local）。`struct P arr[n]; arr[i].m` が
  E3005「左辺は構造体/共用体でない」で弾かれていた。register_vla_lvar_and_append_alloc
  の呼び出し元が psx_decl_set_var_tag を呼んでおらず、VLA lvar の tag_kind が EOF のまま
  だった。`!size_ok` 分岐 + 続き31 の mixed redirect 分岐の両方で tag を carry するよう修正。
- **extern global の GOT 経由参照**（extern_global_got）。`extern FILE *__stderrp;` 等の
  「宣言のみ」global を @PAGE/@PAGEOFF 直参照してリンクが "does not have address" で失敗。
  続き4 の関数アドレス GOT 化と同じパターンを extern data に拡張: ir_builder に
  emit_load_sym_for_gvar を新設し、psx_find_global_var で gv_ent を引いて is_extern_decl
  が立つときは is_got_funcref を立てる。LOAD_SYM 発行サイト 5 箇所をヘルパに集約。
  副次効果: stdio.h に stdin/stdout/stderr (Apple libc の __std{in,out,err}p) を追加し、
  `fprintf(stderr, ...)` が使えるように。
`make test`=1042/1042 green。

## このセッション（続き34）: 3D+ VLA 仮引数
- **3D+ VLA 仮引数**（vla_3d4d_param）。`int sum_3d(int n, int m, int k, int t[n][m][k])`
  のような 3D 以上の VLA 仮引数で内側 dim が silently 切り捨てられ miscompile していた。
  parse_param_declarator_name_recursive は inner_first_dim / inner_second_dim の 2 個しか
  捕捉せず、register_vla_array_param も 2D までしか stride を計算していなかった。
- 修正:
  - parser.c: 内側 dim を最大 7 個 g_param_inner_dim_consts / g_param_inner_dim_idents に保存。
  - lvar_t: vla_param_inner_dim_consts[7] / src_offsets[7] / count を追加。
  - register_vla_array_param: N-D VLA 仮引数で stride スロット (n_inner*8 バイト) を anon lvar
    `__rs_<name>` として確保し、vla_strides_remaining = n_inner - 1。全 const 内側 (`int a[][2][3][4]`)
    は extra_strides も使う既存非 VLA 経路に近い形で初期化。
  - ir_builder.c emit_vla_row_stride_for_params: N-D VLA 仮引数の各 level の stride を関数
    entry で計算・store。後ろから掛けて各 level 1 回の MUL で済む構成 (const dim は IR_MUL の
    immediate、runtime dim は param frame slot から load)。
- subscript chain / sizeof は続き33 の local N-D VLA 機構 (vla_row += 8 / vla_strides_remaining
  -= 1) をそのまま流用。3D 全 VLA / 4D 全 VLA / 4D mixed const/VLA / 3D 全 const 内側を
  fixture で網羅。`make test`=1040/1040 green。

## このセッション（続き33）: 4D+ VLA / 汎用 N-D 対応
- **4D+ VLA**（vla_4d_and_higher）。`int t[n][m][k][l]...` の 4 次元以上が E3064 で拒否されていた。
  続き30 の 3D 用 vla_mid_stride_frame_off を汎用 `vla_strides_remaining` + 連続 stride スロット
  に置換し、最大 8 次元 (実用範囲) の VLA をサポート。
- 設計:
  - descriptor slot = 16 + 8*(N-1) バイト。slot+0=base ptr、slot+8=byte_size、slot+16..slot+16+8*(N-2)
    に N-1 個の runtime stride を保存。stride[k] = dim[k+1]*dim[k+2]*...*dim[N-1]*elem。
  - level 0 は ND_VLA_ALLOC の rsf 経路で初期化、level 1..N-2 は init_chain への STORE 注入で初期化。
  - lvar_t / node_mem_t に `vla_strides_remaining` を追加。subscript chain で
    vla_row_stride_frame_off を +=8 シフト、remaining を -=1 で消費。
  - `inner_deref_size = elem` を chain に carry することで、最終 runtime stride 消費後も
    subscript_base_address_of が「中間配列」を正しく認識 (これがないと SIGSEGV)。
  - sizeof(vlaN[i][j]...[d]) は連続 [...] を D 段 peek して slot+16+(D-1)*8 を読む統一経路。
- 4D 全 VLA、4D mixed const/VLA、5D 全 VLA、4 段 sizeof を fixture で網羅。
  `make test`=1039/1039 green。

## このセッション（続き32）: 探索 round 20 probe 全 green
3D VLA + 混在 const/VLA 対応後、未探索の角度で probe を 20 件流して**新規 miscompile 0 件**。
カバー領域は `bug_coverage.md` の「2026-06-22 セッション 続き32」に索引化済み (再探索不要)。
網羅したカテゴリ:
- 関数ポインタ戻り値・関数ポインタ配列メンバ + 集約初期化
- qsort 複雑 comparator (struct + tie-break)
- 複合代入チェーン (構造体メンバ × 配列要素 × shift)
- snprintf format flags 細形 (`#`/`0`/precision/`e g a`)
- ポインタ配列 + 負添字 + 文字列処理
- 64bit ビット演算 (64bit × 64bit unsigned 乗算)
- 可変長引数 double 列の交互加減算
- bitfield + cast (`(unsigned)(signed bitfield)`)
- 混在幅比較・再帰 struct list (malloc chain)
- switch 4-way fallthrough + default
- designator init array gap (positional 混在)
- extern + 同一 TU 定義、マクロ stringize/paste、volatile + ++、
- const 関数ポインタ typedef、goto labels、ternary が struct 値
- 大 struct 値渡し + scalar 混在

## このセッション（続き31）: 混在 const/VLA dim サポート
- **混在 const/VLA dim**（vla_mixed_dims）。`int t[2][n][4]` のように第 1 dim が const で
  後の dim が VLA の混在配列が E3064 で弾かれていた。第 1 dim が const の場合は
  register_multidim_array_lvar 経由になり、parse_decl_constexpr_array_suffix_product_n が
  VLA dim を「非定数」と判定して E3064。C11 6.7.6.2 では「次元式のいずれかが非定数なら配列
  全体は VLA」だが、ag_c は第 1 dim だけで VLA か否かを判定していた。
- 修正: decl_peek_trailing_array_dims_have_vla を新設し、後続 `[...]` を token peek で走査。
  TK_IDENT (enum 定数以外) が含まれていれば VLA 経路へ redirect。const 第 1 dim を ND_NUM
  ノードに包んで size_node として register_vla_lvar_and_append_alloc に渡す。enum 定数は
  psx_ctx_find_enum_const で識別して定数扱いし、誤検出を避ける。誤検出した場合も VLA 経路の
  const-inner 分岐で正しく動く (frame slot は VLA 用の 16/24/32 バイト分使う)。
- 2D `int a[2][n]`・3D `[C][n][C]`・`[C][C][n]`・全 VLA・enum 定数 dim を fixture で網羅。
  `make test`=1038/1038 green。

## このセッション（続き30）: 3D VLA 宣言と subscript chain / sizeof
- **3D VLA**（vla_3d）。`int t[n][m][k]` が E3064 で弾かれていた。
  register_vla_lvar_and_append_alloc が 1D/2D のみ対応で、3 段目の dim suffix を
  parse_decl_skip_constexpr_array_suffixes で消費しようとして非定数を拒否していた。
  以下のセットで対応:
  - 32B descriptor slot に拡張: [base ptr][byte_size][outer_stride][mid_stride]。
    outer = m*k*elem (vla_row_stride_frame_off に格納、既存 rsf 経路で初期化)。
    mid = k*elem (vla_mid_stride_frame_off に格納、init_chain に STORE 注入で初期化)。
  - lvar_t に vla_mid_stride_frame_off フィールドを追加。
  - build_lvar_or_vla_node: 3D VLA は inner_deref_size=0 (1 段目 subscript の結果が
    「次 stride も runtime」と知らせる) / next_deref_size=elem (3 段目用)。
  - build_subscript_deref: ND_LVAR (3D VLA) の subscript 結果 ND_DEREF に
    vla_row_stride_frame_off=mid_slot を立てる。続く `t[i][j]` の make_scaled は
    ND_DEREF 経由で vla_rsf を読み runtime mid stride でスケール。
  - make_subscript_scaled_offset が ND_DEREF からも vla_rsf を読む (従来 ND_LVAR のみ)。
  - subscript_base_address_of が vla_row_stride_frame_off>0 の deref を address (lhs)
    として返す。これがないと t[i] が 1 バイト load されて SIGSEGV。
  - sizeof(vla3d[i][j]) の特別経路: 2 段添字を peek し vla_mid_stride_frame_off スロット
    (k*elem) を 8B unsigned long として返す。1 段は既存 vla_row 経路、3 段は要素 (elem 定数)
    なので fallthrough。
- all-VLA `int t[n][m][k]`・first-dim VLA `int t[n][3][4]`・double 要素・read/write・
  3 段 sizeof を fixture で網羅。`make test`=1037/1037 green。
- **未対応 (既知制約として bug_coverage に記録)**: 第 1 dim が const で後の dim が VLA
  (例 `int t[2][n][4]`) は register_multidim_array_lvar 経由のため依然 E3064。const-first
  経路を VLA 経路に切り替える必要があり、別タスクで対応する。

## このセッション（続き29）: typedef chain dims 合成 + 関数内 typedef is_array
- **typedef chain で基底が配列の場合の dims 合成**（typedef_array_chain）。
  `typedef int Row[3]; typedef Row Matrix[2]` のような chain で Matrix が int[2] として登録され
  sizeof(Matrix)=24 のはずが 8、`Matrix m={{1,2,3},{4,5,6}}` も E3064。トップレベル
  (parser.c define_toplevel_typedef_from_declarator) と関数内 (stmt.c parse_typedef_decl) の両方
  で、base typedef の array_dims (= [3]) と declarator の dims (= [2]) を `[declarator..., base...]`
  の順で結合して新しい typedef の dims/sizeof を更新するよう修正。pointer-to-array typedef
  (`typedef int (*PA)[3]`、ptr_in_paren_group=1) とは排他。stmt.c では基底が配列 typedef のとき
  _ti.array_dims を g_stmt_base_array_dims にコピーする経路を parse_decl_type_spec に追加。
- **関数内 typedef の通常配列の is_array**（同じ fixture で網羅）。
  stmt.c parse_typedef_decl が通常の配列 typedef `typedef int Row[3]` でも is_array=1 を立てて
  おらず (line 246: `is_array = is_base_ptr_arr ? 1 : 0`)、トップレベル parser.c とは非対称。
  関数内で `Row r = {1,2,3}` が E3064。`is_plain_array`(`!is_ptr && arr.is_array && arr.dim_count>0`)
  分岐を追加し is_array/dims を立てるよう修正。
- 3 段 chain (A→B→C)・基底が多次元 (`typedef int M23[2][3]; typedef M23 M4[4]`)・declarator が
  多次元 (`typedef Row Cube[2][5]`)・グローバル変数・flat init・関数内 chain を fixture で網羅。
  `make test`=1036/1036 green。

## このセッション（続き28）: 配列へのポインタ経由の struct メンバ access
- **`struct S (*ap)[N]; (*ap)[i].m`** が E3005 で弾かれていた（ptr_to_array_struct_member）。
  原因: ap (lvar) は宣言時に tag_kind=STRUCT を持つが is_tag_pointer=0（変数自体はポインタ-to-配列で
  あって tag ポインタではない）。build_unary_deref_node 冒頭の `tag_kind != TK_EOF && is_tag_ptr`
  ガードが偽となり、tag が ND_DEREF に carry されない。結果として `(*ap)` の psx_node_get_tag_type
  が TK_EOF を返し、build_subscript_deref で subscript 結果に tag が立たず、member access が
  E3005「'.' の左辺は構造体/共用体である必要があります」で失敗。`struct S s = (*ap)[1]` の struct
  値コピー経路は memcpy ベースで tag 不要のため動作していた（差分でバグが顕在化しにくかった）。
  修正: build_unary_deref_node の outer_stride 検出ブロック内 (1D / 2D 両方) で、
  `src->tag_kind != TK_EOF && !src->is_tag_pointer` のとき deref ノードに tag を carry
  （is_tag_pointer=0: 結果は配列で、要素が struct）。これで `(*ap)[i].m` の subscript+member
  解決が通り、メンバ read/write・仮引数経由・2D `(*ap2)[i][j].m`・union 要素 `(*up)[i].s.a`・
  グローバル `gap` のいずれも green。`make test`=1035/1035 green。

## このセッション（続き27）: ポインタ算術後の deref で pql/bds carry
- **`*(pp + n)` の pql/bds carry**（struct_double_ptr_deref_arrow）。
  `struct P **pp; (*(pp + n))->m` が E3005。build_unary_deref_node が
  psx_node_pointer_qual_levels(operand) を呼ぶが ND_ADD/ND_SUB を考慮しておらず pql=0 を返し、
  「pql>=2 の多段 deref」分岐に乗れず struct ポインタ扱いされなかった。
  修正: psx_node_pointer_qual_levels / psx_node_base_deref_size の switch に ND_ADD/ND_SUB
  分岐を追加し、ポインタ側 (lhs 優先、rhs fallback) の pql/bds を carry。これで `*(pp+n)`
  が struct P* として認識され `->` 解決可能。`make test`=1034/1034 green。

## このセッション（続き26）: グローバル struct-ptr-array init slot 計算
- **タグポインタ配列の 1 slot 化**（global_struct_ptr_array_subscript 拡張）。
  `struct P *parr[3] = {&pts[0], &pts[1], &pts[2]}` で parr[1]/parr[2] のシンボル+offset が
  誤値だった (gbrace_child_at が tag_kind=STRUCT && is_array=1 で struct 値 (= 内側メンバ数
  slot) として展開していたため)。
  修正: gbrace_ctx_t に is_tag_pointer フィールドを追加 (gv->is_tag_pointer / mi->is_tag_pointer
  から carry)。gbrace_child_at で `ctx.is_array && ctx.is_tag_pointer` のとき scalar 8B
  ポインタ slot (TK_EOF, elem_size=8) を返す。境界揃え (positional) と `[N]=` の elem_slots
  も `!is_tag_pointer` でガード。

## このセッション（続き25）: VLA sizeof の variadic 引数経路
- **VLA sizeof の scalar 化**（vla_sizeof_direct）。
  `int arr[sz]; printf("%zu", sizeof(arr))` が garbage。parse_sizeof_operand が VLA 全体
  サイズスロットを指す ND_LVAR を返すが、IR builder の variadic 引数経路 find_owning_lvar
  が arr_var (VLA メタ slot サイズ=16) を所属判定して「struct 16B 値渡し」扱いに化け、
  2 slot 渡しで garbage が混じっていた。
  修正: VLA 全体サイズ + 行サイズの sizeof 返り値を ND_PTR_CAST でラップし、scalar 8B
  unsigned long として明示。find_owning_lvar の所属判定を回避して variadic 経路で 8B
  1 slot として正しく渡される。中間変数経由 (`long s = sizeof(arr)`) は元から動作。

## このセッション（続き24）: 探索 round 4 + 2 件修正
- **明示キャスト経由のポインタ初期化**（`void *p = (void*)0xdeadbeefL`）。
  apply_cast が folding で ND_NUM に潰し、init check (C11 6.5.16.1) が「非ゼロ整数定数で
  ポインタ初期化」E3064 を発火。node_num_t に from_pointer_cast フラグを追加、apply_cast
  でポインタ cast 結果が ND_NUM になる時にスタンプ、decl.c の check で skip。
  プレーン `int *p = 42;` (キャストなし) は引き続き弾く。
- **グローバル struct ポインタ配列の subscript + ->**（global_struct_ptr_array_subscript）。
  `struct P *parr[N] = {...}; parr[i]->m` が E3005 (-> 左辺が struct ポインタじゃない)。
  try_build_global_var_node の配列 decay 経路で is_tag_pointer のとき pql=1/bds=struct サイズ
  を立て、subscript 結果が「要素はポインタ」分岐に乗るように。emit 側も struct-array 経路に
  `!gv->is_tag_pointer` ガードを追加し、scalar emit (8B ポインタ) に流す。
  fixture は parr[0] と local 変種のみ網羅 (parr[1]/parr[2] の init slot 計算は別バグで残課題 2)。
- bug_coverage.md に round 4 探索領域 + 残課題 3 件を索引化済み。

## このセッション（続き23）: 探索 round 3 + 2 件修正
未探索角度 12 probe を流して 10 件 green / 2 件発見・修正:
- **typedef-array-with-pointer-elements 宣言**（typedef_pointer_element_array_decl）。
  `typedef IP IPA[3]; IPA arr = {&x, &y, &z}` が「スカラ初期化子 1 要素のみ」E3064 で失敗。
  base が pointer typedef + typedef 自体が配列のとき、declarator に `*` を追加していない場合
  is_pointer を 0 にリセットして配列宣言経路へ。register_typedef_array_lvar に td_array_elem_size
  (= 8) を渡し、pointer_qual_levels=1 / base_deref_size=pointee サイズを立てて `*arr[i]` を
  pointee で deref。多次元 typedef (`typedef int M[2][3]; M m`) は td_array_dim_count==1 で除外。
  stmt.c の parse_typedef_decl にも base-is-ptr-only 経路を追加 (parser.c は続き20 で対応済み)。
  ローカル typedef chain (Int→Score→ScorePtr→ScorePtrArr) も動くように。
- **struct メンバ位置の _Static_assert**（static_assert_in_struct）。C11 6.7.2.1 で許可される
  `struct S { _Static_assert(expr, "msg"); int x; };` が「メンバ型が必要」E3064。struct_layout の
  メンバ解析冒頭に TK_STATIC_ASSERT 分岐を追加 (トップレベル/関数内経路と同じく expr を畳み込んで
  偽なら診断)。ネスト struct でも動く。
- bug_coverage.md に round 3 探索領域 (C11 機能 / 言語機能各種) を索引化済み (再探索不要)。
- offsetof on bitfield は仕様外 (clang もエラー、未定義動作) で probe から除外。

## このセッション（続き22）: ネスト union fp 初期化の sentinel 化
- **designator sentinel**（nested_union_designator_ordinal）。続き19 のヒューリスティック
  (fv!=0 && iv==0) を sentinel 機構に置き換え、`.f = 0.0f` と `.n = 0` の判別、union 内に
  float/double 両方ある場合の正確な type_size 判定を可能にした。gbrace_ctx_t に
  pending_fp_kind / pending_fp_size を追加し、DOT 経路で union fp 設定 → scalar 書き込み時に
  init_value_symbol_lens に sentinel (-2=float / -3=double) をスタンプ。emit TK_UNION 分岐が
  sentinel を最優先で読み取り、無ければ旧ヒューリスティックに fallback。`make test`=1029/1029 green。

## このセッション（続き21）: IP (*pia)[3] 最終 deref サイズ伝播
- **データポインタ要素配列の deref**（ptr_to_array_of_funcptrs 拡張）。`*(*pia)[0]` の直接比較
  `== 100` が型情報ずれで 0 を返していた。decl.c の paren `(*p)[N]` / typedef-array `A *pa`
  両経路で「要素がデータポインタ」のとき base_deref_size を pointee サイズ (int=4) に、
  pointer_qual_levels=1 を立てる。build_unary_deref_node で `*p` の配列 decay 経路 (2906 行)
  が src の pql/bds を carry し、build_subscript_deref の「要素はポインタ」分岐に乗って結果が
  scalar pointer (deref_size=4) になる。関数ポインタ要素 (bl で 8B 値そのまま使う) は除外。
  `make test`=1028/1028 green。

## このセッション（続き20）: typedef BinOp OpArr3[3] の sizeof / is_array
- **pointer-element 配列 typedef の登録**（typedef_pointer_element_array_sizeof）。base が
  pointer typedef + 宣言子に `*` 追加なし + 配列 suffix のとき、sizeof_size を 8*N、
  is_array=1 として登録するよう parser.c (compute_toplevel_typedef_sizeof /
  define_toplevel_typedef_from_declarator) と decl.c (define_local_typedef_from_declarator) を
  改修。expr.c の sizeof 経路も `(!td_ptr || td_is_array)` で sizeof_size を読むように。
  pointer-to-array typedef (`typedef int (*PA)[3]`、ptr_in_paren_group=1) とは排他。
  `make test`=1028/1028 green。

## このセッション（続き19）: グローバル struct のネスト union fp メンバ初期化
- **ネスト union fp 初期化**（global_struct_nested_union_fp）。
  `struct Inner { int a; union { int n; float f; } u; }; struct Inner o = { 2, {.f = 2.5f} };`
  で `o.u.f = 0.0` に化けていた。emit_global_struct_members_rec に TK_UNION 分岐を追加し、
  ヒューリスティック「fv!=0 && iv==0 なら fp 経路」で内側 union の fp メンバを active と推定
  して emit する (psx_gbrace_flat が ND_NUM 整数も `(double)val` を fv に書くため、fv 単独
  では `.n = 99` を fp 扱いしてしまう。iv==0 で絞る)。`make test`=1027/1027 green。
  **限界**: 上記「次セッションの最優先タスク」3 参照。

## このセッション（続き18）: 関数ポインタ配列へのポインタの宣言子解析
- **`(*p)[N]` の要素サイズ**（ptr_to_array_of_funcptrs）。
  `BinOp (*pa)[3] = &ops; (*pa)[0](7,2)` が SIGSEGV。decl.c の `(*p)[N]` 経路が elem_size を
  常に base 戻り型 (int=4) として登録し、関数ポインタ要素 (8B) を 4B (`ldrsw`) で load して
  下位 4B だけで `bl` していた。修正: 同経路で eff_elem を 8 に上書きする条件を追加:
  (a) 宣言子の trailing 部に関数シグネチャ `(args...)` がある (g_decl_trailing_func_suffix を
  新設、consume_decl_name_recursive の skip_func_params ループで立てる)。
  (b) 基底型 typedef がポインタ型 (base_is_pointer) → `BinOp (*pa)[N]` / `IP (*pa)[N]` 形式。
  base_deref_size / outer_stride / mid_stride すべて eff_elem を使う。`make test`=1026/1026 green。
  **限界**: 上記「次セッションの最優先タスク」1, 2 参照。

## このセッション（続き17）: 探索 round 2
タグ shadowing 完了後、未探索の角度で probe を 19 件流して新規 2 件発見:
- libc string/math/malloc/qsort/va_list/static local struct/recursive list/const struct/offsetof/
  array decay/hex float/string concat/bitfield+union/typedef+funcptr/ternary+struct/VLA 多次元・
  short ポインタ算術・nested struct init・function-local static counter
- すべて green の 17 件は `docs/differential_testing/bug_coverage.md` の 2026-06-22 節 (round 2)
  に索引化済み (=再探索不要)。新規発見の 2 件は続き18/19 で消化済み。

## このセッション（続き16）: タグ shadowing 応用形
- **変数宣言時 scope の保持**（tag_shadowing_advanced）。続き15 で同スコープ内 shadow の基本形は
  対応したが、応用形 2 件が残っていた:
  (a) ネスト 2 段 shadow: 内側 1 で宣言した変数 `s` を内側 2 (さらに別 S を shadow) から参照
  すると `s.p` が E3064。
  (b) 内側スコープから外側 tag のグローバル変数 (`sg`) のメンバを参照すると同様に E3064。
  原因: lvar_t / global_var_t / node_mem_t が tag_kind/name/len しか持たず、宣言時の
  tag_scope_depth を覚えていなかった。build_member_access は find_tag_type で「最も内側」を
  取得するため変数の宣言時タグと参照時タグがズレていた。
  修正: 3 構造体に **tag_scope_depth_p1** (+1 エンコード、0=未設定の規約) を追加。
  psx_decl_set_var_tag / _set_gvar_tag で宣言時に psx_ctx_get_tag_scope_depth から取得して
  保存。識別子参照ノード構築 (new_typed_lvar_ref / build_lvar_or_vla_node /
  build_array_lvar_addr_node / try_build_global_var_node / static-local lowering) で var/gv
  → node に伝播。新 API **psx_ctx_{get,find}_tag_member_info_at_scope** /
  **psx_ctx_get_tag_scope_depth** を追加し、build_member_access が
  psx_node_get_tag_scope_depth(base) で取り出した scope を渡して「変数が宣言時に見ていた tag」
  のメンバを引く。tag_scope_depth_p1=0 (未設定) の場合は従来挙動 (最も内側 tag) に fallback。
  `make test`=1025/1025 green。

## このセッション（続き15）: タグ shadowing 基本形
- **内側ブロックでの同名 struct 再宣言**（tag_shadowing_block_scope）。
  `struct S{int a;int b;}; int main(){ { struct S{double x;double y;}; struct S s2={1.5,2.5}; ...}}`
  で内側 s2 の初期化が外側 S のレイアウトで行われ s2.x が 2.5 (本来 1.5)、外側に戻った後の s3 も
  壊れていた。psx_ctx_define_tag_type_with_layout が同名タグを問答無用で update していた (member_count
  が増えるなら更新、それ以外は据え置き)。修正: existing->scope_depth と tag_scope_depth が一致する
  ときだけ in-place update (前方宣言→定義)、異なれば新規挿入 (先頭 push)。leave_block_scope は既に
  scope_depth>=old_depth を削除する設計なので shadow は scope を抜けると自然消滅。
  メンバ lookup (get_tag_member_info / find_tag_member_info) も「find_tag_type で確定した
  最も内側 tag の scope_depth と一致するメンバのみ」を返すよう改修 (混在防止)。
  基本形 (同スコープでの宣言+参照、shadow 後に外側へ戻る、union のメンバアクセス含む) を網羅。
  `make test`=1024/1024 green。
  **限界 (応用形は次セッション)**: 上記「次セッションの最優先タスク」参照。lvar_t/global_var_t に
  宣言時 scope_depth を持たせる追加機構が要る。

## このセッション（続き14）: グローバル struct の fp 配列メンバ
- **fp 配列メンバの emit**（global_struct_fp_array_member）。
  `struct R{double m[2][2];}; struct R r = {.m = {{1.5,2.5},{3.5,4.5}}};` で `.quad 0` だけが
  並び double 値が全て 0 になっていた。emit_global_struct_members_rec の配列メンバ経路 (タグ無し
  非ポインタ要素) が `cg_emit_int_directive(ts, ev)` を直接呼び、`mi.fp_kind` と `efv` (init_fvalues
  由来の double 値) を見ずに整数 ev=0 を出力していた。配列メンバ経路を `emit_global_init_member_scalar`
  に統一し、fp_kind/efv を見るようにする (シンボル/関数ポインタ経路と同じ処理)。1D float、スカラ先頭、
  後続スカラを網羅。`make test`=1023/1023 green。

## このセッション（続き13）: 多次元配列メンバへのネスト designator
- **`[N]={[M]=...}` の slot 計算**（global_multidim_array_nested_designator）。
  `struct P{int x[3][3];}; struct P p = {.x = {[0]={1,2,3}, [2]={[2]=9}}};` で 9 が `p.x[2][2]`
  (slot 8) ではなく `p.x[1][1]` (slot 4) に書かれていた。psx_gbrace_flat の `[N]=` 経路が
  「scalar 要素配列」(elem_slots=1) として処理し、`[2]=` が slot 6 ではなく slot 2 へジャンプ。
  続き10 で導入した arr_dims/sub_dims 機構を char 限定から非 char にも一般化 (struct_layout で
  arr_dims 保存、gbrace_ctx_from_member の sub_dims 設定、gbrace_child_at で int 配列にも対応)。
  `[N]=` 経路で ctx.sub_ndim>0 のとき elem_slots = 内側次元の総スカラ数 (sub_dims の積) として
  計算。2D int / 3D int を網羅。`make test`=1022/1022 green。

## このセッション（続き12）: 多次元 char 配列メンバへの brace elision
- **brace elision (global+local)**（multidim_char_member_brace_elision）。
  `struct B{char rows[2][4];}; struct B b = {"ab","cd"};` (C11 6.7.9p20: 1 文字列 = 1 行、内側
  brace なし) が両系統で壊れていた。
  - (ローカル) parse_struct_initializer が 1 メンバ分のみ try_parse_array_member_string_initializer
    に委譲、最初の文字列で配列全体を埋め return、後続文字列が E3064。
  - (グローバル) psx_gbrace_flat の TK_STRING 分岐が `row_w = child.array_len` (= メンバ全要素数) を
    使い 1 文字列で配列全体を埋め、後続文字列が次メンバ扱い。
  修正: (ローカル) parse_member_initializer に brace elision 専用分岐 (arr_ndim>=2, elem_size==1,
  curtok==TK_STRING) を追加し、行ごとに文字列を消費。(グローバル) `row_w = child.sub_dims[最後]`
  を使うように変更し、次反復で gbrace_child_at が同メンバを返すため自然に複数行が処理される。
  2D/3D × global/local を網羅。`make test`=1021/1021 green。

## このセッション（続き11）: 関数内 struct の 2 次元 char 配列メンバ
HANDOFF 残課題「ローカル (非 static) struct の 2D char メンバ」を修正。
- **ローカル 2D char 配列メンバ**（local_struct_2d_char_array_member）。`parse_member_initializer`
  (decl.c) の多次元配列メンバ・ネスト brace 経路が、行要素として文字列リテラル ("ab" / "cd") が
  来たとき parse_scalar_brace_initializer で 1 個のスカラ値として読み、文字列リテラルを `.LC0`
  ラベルアドレスの下位 1 バイトとして 1 slot に書き込んでいた (`strb w20, [x19]`)。グローバル経路
  (psx_gbrace_flat) は既に修正済み (続き8 の global_struct_2d_char_array_member) だったが、ローカル
  経路は同じ機構を持っていなかった。
  修正: 同経路で `elem_size==1 && val->kind==ND_STRING` のとき、グローバル経路と対称な処理で文字列を
  inner_len バイトへバイト展開して flat に書き込み、行ぶん flat を進める (EMIT_ROW_FROM_STRING マクロ)。
  `{{"ab","cd"}}` (内側 brace あり) と `{"ab","cd"}` (外側 brace 内で直接並ぶ) の両形に対応。
  続き12 で brace elision も別途修正、続き13 で 3D ローカルも対応。`make test`=1019/1019 green。

## このセッション（続き10〜11.5）: グローバル struct の 3 次元 char 配列メンバ / 3D ローカル
- **グローバル 3D 以上の char 配列メンバ**（global_struct_3d_char_array_member、続き10）。
  `struct{char c[2][2][3];} g` が **2 系統で同時に壊れていた**:
  (a) 初期化側: gbrace_ctx_t が 1 段ぶんの行幅 (row_width) しか持てず、3D 以上の brace 構造を表現できず
  各内側 brace の文字列が「要素 (char)」扱いされ array_len=0 でポインタ化 (.LC ラベル) されていた。
  (b) アクセス側: tag_member_info_t に mid_stride がなく、build_member_deref_node の 3 段 subscript で
  中間ストライドが立たず誤アドレスを deref → SIGSEGV。
  修正: tag_member_info_t / tag_member_t に **arr_dims[8] / arr_ndim** (各次元サイズ) と **mid_stride**
  (1 段 subscript 後の要素サイズ) を追加。struct_layout で 3D 以上の char メンバ時に arr_dims、3D 以上
  の任意メンバ時に mid_stride をセット (匿名 struct 昇格でも伝播)。gbrace_ctx_t に **sub_dims チェーン**
  を持たせ、gbrace_child_at が 1 段消費。build_member_deref_node で 3D 以上は
  inner_deref_size=mid_stride / next_deref_size=elem_size を立ててローカル多次元配列と同じ 3 段
  subscript 表現に乗せる。`make test`=1018/1018 green。
- **ローカル 3D 以上の char 配列メンバ**（local_struct_3d_char_array_member、続き13）。
  parse_member_initializer の多次元 brace 経路が outer_stride (1 段目 subscript の幅) しか持たず、
  行自体がさらに 2D 配列となる 3D 以上で内側構造を見れなかった。シグネチャに member_arr_dims/
  member_arr_ndim を追加し、3D 以上は新しい再帰関数 parse_multidim_char_member_brace に委譲。
  グローバルの gbrace_ctx_t.sub_dims チェーンと同等の機構で、最内 1 次元を char 行 (文字列展開)
  として扱い ndim>=2 は brace ごとに 1 段消費して再帰。`make test`=1020/1020 green。

## このセッション（続き11）: 関数内 struct の 2 次元 char 配列メンバの文字列初期化
HANDOFF 残課題 2「ローカル (非 static) struct の 2D char メンバ」を修正。
- **ローカル 2D char 配列メンバ**（local_struct_2d_char_array_member）。`parse_member_initializer`
  (decl.c) の多次元配列メンバ・ネスト brace 経路が、行要素として文字列リテラル ("ab" / "cd") が
  来たとき parse_scalar_brace_initializer で 1 個のスカラ値として読み、文字列リテラルを `.LC0`
  ラベルアドレスの下位 1 バイトとして 1 slot に書き込んでいた (`strb w20, [x19]`)。グローバル経路
  (psx_gbrace_flat) は既に修正済み (続き8 の global_struct_2d_char_array_member) だったが、ローカル
  経路は同じ機構を持っていなかった。
  修正: 同経路で `elem_size==1 && val->kind==ND_STRING` のとき、グローバル経路と対称な処理で文字列を
  inner_len バイトへバイト展開して flat に書き込み、行ぶん flat を進める (EMIT_ROW_FROM_STRING マクロ)。
  `{{"ab","cd"}}` (内側 brace あり) と `{"ab","cd"}` (外側 brace 内で直接並ぶ; 内側 brace なしの
  brace elision の一種) の両形に対応。`make test`=1019/1019 green。
  **限界 (未対応、次タスク候補)**: (1) 3 次元以上 `char c[2][2][3]` のローカル版は行自体がさらに 2D
  配列のため未対応 (続き10 の global と同じ機構を parse_member_initializer に移植する必要)。
  (2) `struct B b={"ab","cd"};` (外側 brace 1 段のみの brace elision) は parse_struct_initializer の
  メンバ数判定で E3064。配列メンバが文字列を複数受け取れる brace elision 分岐が要る。

## このセッション（続き10）: グローバル struct の 3 次元 char 配列メンバ
HANDOFF 残課題 1「3 次元以上の char 配列メンバ (グローバル)」を修正。
- **グローバル 3D 以上の char 配列メンバ**（global_struct_3d_char_array_member）。`struct{char c[2][2][3];} g`
  が **2 系統で同時に壊れていた**:
  (a) 初期化側: gbrace_ctx_t が 1 段ぶんの行幅 (row_width) しか持てず、3D 以上の brace 構造を表現できず
  各内側 brace の文字列が「要素 (char)」扱いされ array_len=0 でポインタ化 (.LC ラベル) されていた。
  (b) アクセス側: tag_member_info_t に mid_stride がなく、build_member_deref_node の 3 段 subscript で
  中間ストライドが立たず誤アドレスを deref → SIGSEGV。
  修正: tag_member_info_t / tag_member_t に **arr_dims[8] / arr_ndim** (各次元サイズ) と **mid_stride**
  (1 段 subscript 後の要素サイズ) を追加。struct_layout で 3D 以上の char メンバ時に arr_dims、3D 以上
  の任意メンバ時に mid_stride をセット (匿名 struct 昇格でも伝播)。gbrace_ctx_t に **sub_dims チェーン**
  を持たせ、gbrace_child_at が 1 段消費 (sub_ndim==1 なら最内 1D char 配列 → 文字列展開、sub_ndim>=2 なら
  ネスト配列を再帰)。build_member_deref_node で 3D 以上は inner_deref_size=mid_stride / next_deref_size=elem_size
  を立ててローカル多次元配列と同じ 3 段 subscript 表現に乗せる。`make test`=1018/1018 green。
  **限界 (続き11 で消化)**: ローカル 2D char メンバは別経路 (parse_member_initializer)。続き11 で対応。

## このセッション（続き9）: グローバル struct 配列の要素メンバにある char 配列
HANDOFF サブケース (c) `struct{char tag[4]; int n;} g[2]={{"aa",1},{"bb",2}}` を修正。
- **グローバル struct 配列要素の char 配列メンバ**（global_struct_array_char_member）。
  emit_global_struct_array_init がメンバごとにフラット slot を 1 個だけ消費する単純ループで、
  配列メンバ (`char tag[4]`)・char 配列の文字列展開・入れ子 struct メンバ・bitfield を扱えなかった。
  tag が 1 バイトしか出ず (`.byte 97; .space 3`)、後続メンバ n に 2 文字目の 'a'(97) が入り総崩れ。
  emit_global_struct_array_init を各要素について emit_global_struct_members_rec を呼ぶ形に書き換え、
  非配列 struct (emit_global_struct_init) と同じメンバ展開機構を要素ごとに適用して統一した
  (配列メンバ/char 配列展開/入れ子 struct/bitfield/部分初期化のゼロ埋めを共通処理)。parser 側
  (psx_gbrace_flat) は元から各要素の char 配列メンバをバイト展開できており emit 側のみの不具合。
  char配列先頭+スカラ・スカラ先頭+char配列・部分初期化ゼロ埋め・入れ子struct要素・char配列2本を
  網羅。`make test`=1017/1017 green。**これで HANDOFF「発見したが未修正」の (a)(b)(c) を全消化。**
  残既知制約: 3 次元以上の char メンバ・ローカル (非 static) struct の 2D char メンバ (別経路)。

## このセッション（続き8）: グローバル struct の 2 次元 char 配列メンバの文字列初期化
HANDOFF サブケース (b) `struct{char rows[2][4];} g={{"ab","cd"}}` を修正。
- **グローバル 2D char 配列メンバ**（global_struct_2d_char_array_member）。2 次元 char メンバの行幅
  (outer_stride=4) が global brace flat パーサのコンテキスト gbrace_ctx_t に伝わらず、ネスト brace
  `{"ab","cd"}` の各行文字列が「要素 (char)」扱いされ array_len=0 でポインタ (.LC ラベル) として
  出力されていた (`.quad .LC0; .quad .LC1`)。gbrace_ctx_t に row_width フィールドを追加し、多次元
  char 配列メンバ (tag 無し・elem 1B・outer_stride>0・array_len>outer_stride) で outer_stride を
  行幅として持たせる。gbrace_child_at が各要素を「内側 1 次元 char 配列 (char[row_width])」として
  返すので、既存の char 配列メンバ展開分岐 (8c8ce2a) に乗り行ごとに row_width バイトへ展開される。
  2D 基本形・後続スカラメンバ・先行スカラメンバ・短い文字列 0 埋めを網羅。`make test`=1016/1016 green。
  **限界 (未対応)**: (1) 3 次元以上の char メンバ `char c[2][2][3]` は gbrace_ctx_t が全次元チェーンを
  持てず SIGSEGV のまま。(2) ローカル (非 static) struct の 2D char メンバは別経路 (ローカル struct
  メンバ初期化) で未対応 (1D ローカルは動作)。いずれも今回の global flat パーサ修正とは別経路。
  **残: HANDOFF「発見したが未修正」の (c) struct 配列内 char メンバは未確認 (次タスク候補)。**

## このセッション（続き7）: fp 宣言の直後に来る tag グローバルの fp_kind 汚染
HANDOFF「発見したが未修正」のサブケース (a)（char[] メンバの後に char* メンバが続く形
`struct{char buf[4]; char*p;} g={"ab","cd"}` で p が `.quad 0` に化ける）を調査した結果、
真因は char[]+char* の slot マッピングではなく、**fp 宣言の直後に宣言される tag グローバルが
前宣言の decl-spec fp_kind を引き継ぐ汚染**だった。
- **tag global の decl-spec fp_kind 汚染**（global_struct_member_after_fp_decl）。トップレベル
  dispatcher `ps_next_function` が tag キーワード始まりの宣言を `parse_toplevel_tag_decl` へ
  直接回す経路で `reset_toplevel_decl_spec_state` を呼ばず、`g_toplevel_decl_fp_kind` が前宣言
  （例: stddef.h の `typedef long double max_align_t;` → string.h 等が間接 include）の DOUBLE の
  まま残り、ここで宣言する struct/union object の fp_kind が DOUBLE になっていた。すると
  グローバル brace init の fp-fold 経路（`gv->fp_kind != NONE`）が**文字列リテラル/関数参照/
  アドレス初期化子を fp 定数(0)として食べ**、後続メンバが NULL/0 に化けた。`parse_toplevel_tag_decl`
  冒頭で手動の extern/static 4 フラグリセットを `reset_toplevel_decl_spec_state()` 呼び出しに
  置換し、宣言ごとに decl-spec 状態を全クリア（tag 情報は install_toplevel_tag_decl_globals が
  後段で再設定）。これで (a) に加え funcref 初期化子・アドレス初期化子・文字列ポインタも同根の
  取りこぼしが解消。**サブケース (a) の真因はこの汚染であり、先行 fp 宣言が無ければ char[]+char*
  の組合せ自体は元から正しく動作していた**（standalone で確認済み）。`make test`=1015/1015 green。
  **残: HANDOFF「発見したが未修正」の (b) 2 次元 char メンバ・(c) struct 配列内 char メンバは
  別問題として未確認（次タスク候補）。**

## このセッションの目的
clang との差分テスト（同一 C ソースを ag_c と clang でコンパイルして exit code を
比較）で ag_c の miscompile / コンパイルエラーを炙り出し、修正して回帰テストを追加する。

## 2026-06-21 セッション（続き）: bug_coverage の ⚠️ 全消化 → 探索で宣言子/型バグ 6 件
現在 `make test` = **1014/1014 E2E green**（unit/parser/preprocess/IR 含む全 green）。
回帰 fixture は `test/fixtures/probes_found_bugs/`、索引は `docs/differential_testing/bug_coverage.md`。

### 前半: 残っていた ⚠️（既知バグ）を全部 🔧 に
- 多段ポインタの fp pointee `double **p; **p`（b90f302）multilevel_pointer_fp_pointee
- ファイルスコープ集約複合リテラルのアドレス `&(struct S){...}`（d47504b）file_scope_aggregate_compound_literal_addr
- グローバルのネスト brace designator `{.items={[2]={.a=7}}}`（3bbba23）global_nested_brace_designator
- `#if 0` 偽分岐の非C トークン skip（adb180e）if0_skip_non_c_tokens。寛容モード+setjmp/longjmp
- **pointer-to-VLA** `int (*p)[m]`（9fa50c0）pointer_to_vla。ランタイム行ストライド機構を local/param 両方で

### 後半: 探索的差分テスト（/tmp に probe を量産→agc_diff_test）で新規発見・修正
バグは**宣言子・関数戻り型**に集中（式・制御フロー・ABI・プリプロセッサは堅牢で probe 全 green）。
- **ポインタ戻り関数の subscript/算術** `g()[i]` / `*(g()+i)`（df7da63）func_pointer_return_subscript。
  parser がポインタ戻り値の pointee 型を覚えず ND_FUNCALL の deref_size=0 で誤スケール。`double* g()` は
  戻り値を d0 から誤読し SIGSEGV、`unsigned char* g()` は符号拡張。semantic ctx の戻り型から導出。
- **storage class 付きタグ戻り関数** `static struct S *f()`（ea308e3）static_tag_return_function
- **配列へのポインタを返す関数** `int (*f())[N]`（19f81f6）func_return_pointer_to_array。pointee 配列次元を
  捕捉し ret_pointee_array_first_dim に記録。続き111 で直接関数版の 2D pointee `[N][M]` も
  ret_pointee_array_second_dim に記録して stride を伝播。
- **配列へのポインタを返す関数ポインタ** `int (*(*fp)())[N]` funcptr_return_pointer_to_array（続き110）。
  callee の関数ポインタ型に戻り pointee 配列次元/要素サイズを保存し、typedef/global/struct member 経由も伝播。
  続き112 で 2D pointee `[N][M]` も second_dim として保存し、直書き global/struct member の
  trailing `[N][M]` を戻り pointee 次元として扱うよう拡張。
- **ファイルスコープ `static <typedef名> 変数`** `static Point p;`（4cdb34b）static_typedef_name_global
- **const/volatile 付きポインタ戻り型** `int *const f()`（9d57b4c）qualified_pointer_return
- **タグ戻り + `(*...)` 宣言子** `struct P (*f())[3]` / `struct R (*f())(int)`（9bd6850）tag_return_complex_declarator。
  is_toplevel/is_tag の宣言子判定を共有ヘルパ is_function_declarator_sig に抽出して統一

### このセッション（続き2）: struct を返す関数ポインタの間接呼び出しを 2 件修正
- **struct を返す関数ポインタの間接メンバアクセス** `op(41).v` / `op(41)->v`（9f1ac04）funcptr_return_struct_member。
  間接呼び出し（callee != NULL）の ND_FUNCALL に戻り tag 型が伝播せず psx_node_get_tag_type が
  TK_EOF を返し E3005。callee の funcptr 変数（tag フィールドに戻り tag を保持）から導出し、戻り値が
  ポインタか否かは pointer_qual_levels で判定（値戻り pql1→ptr0 / ポインタ戻り pql2→ptr1）。
  値戻り/ポインタ戻り・deref形 `(*op)(x).v`・global funcptr・union 戻り・8B ネストメンバ連鎖を網羅。
- **1/2/4/8B 以外（非 pow2）の struct を返す関数ポインタの間接呼び出し**（ca5af1f）funcptr_return_large_struct。
  x8 ret_area 間接返し ABI が direct call 限定で、間接は IR build 失敗（メンバアクセス以前に
  `struct Big r=ob(100);` 単独でも）。3 箇所修正: (1) parse_call_postfix が間接 funcall に ret_struct_size を
  未設定→callee 戻り tag（pql<=1）からサイズ導出。(2) build_assign_struct の間接 fail を汎用 funcall 経路へ
  委譲し ret_area→dst memcpy。(3) build_node_funcall の ret_area 確保 `!fn->callee` ゲートを撤廃。
  12B/16B/20B struct・16B union・代入・直接メンバ・deref形・global・値引数渡しを網羅。codegen は元から
  x8 設定と blr を独立に出せていた（IR builder 側のゲートのみが原因）。
- **多段ポインタを返す関数の直接 deref** `int **g(); **g()`（d5ceb72）multilevel_pointer_return。
  semantic ctx の ret_is_pointer が bool（段数なし）で `int **` を単段 `int *`（pointee 4B）扱い→
  `*g()` が int になり `**g()` が int 値をアドレス参照で SIGSEGV。型付き変数経由は元から可。戻り型の
  `*` 段数を ret_pointer_levels に記録し、node_utils の funcall 経路（pointer_qual_levels /
  base_deref_size / ps_node_deref_size）が段数>=2 のとき `*g()` を「1 段減らしたポインタ」として組む。
  build_subscript_deref も funcall base を 1 段消費（`g()[i]`→int*）。int**/char**/int***・prefix deref・
  deref+subscript 混在 `(*rg())[1]`・直接 subscript `rg()[0][i]` を網羅。単段ポインタ戻りは不変（段数>=2 ゲート）。

### このセッション（続き2）の追加修正
- **extern 宣言＋同一TU定義（tag/typedef 基底）** `extern struct S es; struct S es={7};`（da88075）
  extern_then_def_same_tu。2 つの独立バグ: (1) storage class フラグ（g_*_is_extern/static）が宣言間で
  リセットされず、前の extern が次の bare-struct 定義に漏れ→finalize の extern 分岐が brace を
  scalar 式として食べ E3064。reset_toplevel_decl_spec_state と parse_toplevel_tag_decl で宣言ごとに
  0 クリア。(2) typedef object 経路（apply_toplevel_typedef_prefix_flags）が extern を無条件 0 に
  していたため `extern T et;` が tentative 定義（.comm）になり本定義と重複 ASSEMBLE_FAIL。extern/static
  を伝播（漏れは (1) の reset で防止）。static の漏れ（`static struct S a; struct S b;` で b が内部
  リンケージ化）も解消。逆順（定義→extern）・別 TU は元から動作。

### このセッション（続き3）: 探索的差分テストで新規 2 件
HANDOFF 列挙の既知未対応を全消化後、/tmp に ~130 probe を量産して agc_diff_test。大半 green
（complex.h・union 配列 init・変則 ptr2d 等、過去の「既知未対応」は集約初期化修正で解消済みと判明）。
新規 miscompile/コンパイルエラーを 2 件発見・修正:
- **ポインタ typedef 仮引数の subscript** `typedef char* Str; len(Str s){ s[i] }`（4d81c8c）
  pointer_typedef_param_subscript。param_decl_spec_t が typedef のポインタ性を捕捉せず、宣言子に
  `*` が無いためスカラ登録され `s[i]` が E3064。deref `*s` は寛容判定で動作・直書き `const char*` も動作。
  typedef 基底のポインタ段数を捕捉し宣言子の `*` と合成して実効ポインタ性を決める。
- **unsigned char/short ポインタ経由の zero-extend** `unsigned char* p; p[i]`（c6ac5d9）
  unsigned_char_pointer_zero_extend。pointee が符号拡張され 200→-56 に化けた。3 経路を修正:
  (1) local subscript の最終要素判定が単段ポインタ (pql=1) を認識できず → fp 中間行判定と対称な
  `!is_pointer && !(inner_ds>0 && es>inner_ds)` に。(2) 仮引数の pointee unsigned を param_decl_spec_t
  に捕捉し var->is_unsigned へ伝播。(3) `*(p+i)` の ND_ADD/SUB を辿る node_pointee_is_unsigned ヘルパ追加。
  signed は符号拡張維持。
- **グローバルの 2 次元以上のポインタ配列** `int *t[2][2]` / `char *names[2][2]` / `int(*t[2][2])(void)`（320e0ff）
  global_2d_pointer_array。`t[i][j]` が SIGSEGV（非ポインタ `int t[2][2]` は動作）。3 修正:
  (1) apply_global_multidim_strides の `!head.is_ptr` ゲートを外し elem_size=8 で stride を立てる。
  (2) build_subscript_deref の pointee_is_scalar_ptr を最終次元(inner_ds==0)のみ load・中間は伝播し
  要素 pointee サイズを base_deref_size で carry。(3) 括弧内配列 `(*t[2][2])` の個別 dims を
  psx_parse_array_suffixes_capture_dims で捕捉。2D/3D データ・char* 文字列・要素代入・2D funcptr を網羅。
- **ローカルの 2 次元以上のデータポインタ配列** `int *t[2][2]`（ff709ed）local_2d_pointer_array。
  register_multidim_array_lvar が outer_stride を立てるが登録後に pql=1/base_deref_size=4 を立てるため、
  build_subscript_deref の「要素はポインタ」分岐が **1 段目** で発火し deref_size を inner_ds(8) から
  bds(4) に上書き → 2 段目が +4/ldrsw に化けた。fp/unsigned と同じ中間行判定 (inner_ds>0 && es>inner_ds)
  で 1 段目を中間行と認識し pointer-element 化を最終次元まで遅延・pql/bds を carry。2D/3D・char*・代入を
  網羅。1D `int *arr[N]`・genuine `int **pp` は不変。
- **ローカルの 2 次元以上の関数ポインタ配列** `int(*t[2][2])(void)`（cf36337）local_2d_funcptr_array。
  ネスト brace init `{{a,b},{c,d}}` が E3064、個別代入でも `t[i][j]()` が SIGSEGV。funcptr 配列の局所登録
  （decl.c:3185）が括弧内 `[N][M]` を inner_array_mul の積に潰し flat 1D 登録で多次元 stride 未設定。
  括弧内個別次元（g_inner_array_dims）を捕捉し outer_stride/mid_stride（要素 8B funcptr）を設定。stride が
  立つことで 2D 配列と正しく認識され brace init も通る（E3064 も解消）。2D/3D・brace init・個別代入・
  値→呼び出し・引数つきを網羅。1D funcptr 配列は不変。**これで 2D ポインタ配列（global/local × data/funcptr）
  を全て解消。**
  **注**: HANDOFF の「既知の差異」末尾の complex.h 欠如は解消済み（include/complex.h
  が存在）。`1.0i` 虚数サフィックス・`__real__` 等は GNU 拡張で対象外（clang は受理するが追わない）。

### このセッション（続き4）: C11 文字列リテラル & 欠落ヘッダ対応
「C11 仕様の網羅状況」を確認 → サブセットと判明。文字列リテラルと欠落ヘッダを順に対応:
- **UTF-16/UTF-32/wide 文字列リテラルの配列初期化** `unsigned short s[]=u"hi"`（9d9f7db）wide_string_literal_init。
  文字定数 `u'A'` は動くのに配列初期化子で壊れた (明示サイズ→値 0、`[]`→E3064、global→.comm 0)。
  3 経路（ローカル init / `[]` 推論 / global init）が要素幅 1 決め打ち。char_width と elem_size 一致時に
  各コード単位を要素幅で格納するよう一般化。ASCII 内容のみ。
- **欠落 C11 標準ヘッダ 10 個を同梱**（2307f83）c11_standard_headers。iso646/stdalign/stdnoreturn/uchar/
  inttypes/fenv/locale/wctype/wchar/tgmath（関数実体はシステム libc）。tgmath 対応で **4 件のコンパイラ
  バグも修正**: (1) `long double` が _Generic 関連型でパース不可（整数 cast-spec が `long` だけ食べる）。
  (2) `double` 制御式が `long double:` に誤マッチ（共に TK_DOUBLE）→is_long_double で区別。(3) 外部関数
  アドレス `fp=sqrt` が adrp @PAGE 直参照でリンク失敗→GOT 経由（@GOTPAGE）。(4) `_Generic(...)(args)` の
  bare funcref 呼び出しが間接化しシグネチャを失い fp 戻り値を x0 で読む→funcref callee を直接呼び出しに
  変換しプロトタイプ ABI を適用。E2E は category-binary 許可リストに wctype/wchar/fenv/locale 関数を追加。
  **C11 残ギャップ**: （下記「続き5」で解消）

### このセッション（続き5）: C11 残ギャップ 2 件を解消
- **非 ASCII の UTF-16/UTF-32 文字列リテラル**（95f095f）wide_string_literal_init 拡張。`U"aあb"` が UTF-8
  バイトをそのまま code unit 化し 6 要素に化けていた。tk_decode_utf8 + 幅対応 tk_next_string_code_units を
  追加し emit / 配列 init(local/global) / 要素数推論の 4 箇所を統一（char/u8=1byte、u=UTF-16 BMP1/補助面
  サロゲート対、U/L=UTF-32）。
- **_Generic で long double を double と区別**（8eaf519）generic_long_double。long long/plain char と同じ
  side-channel ビット (node_mem_t.is_long_double) を宣言時に立てノードへ伝播し infer_generic_control_type が
  読む。fp_kind は DOUBLE のままで codegen 不変、値は同一、_Generic 選択のみ変わる。ローカル変数・cast で
  機能（long long と同じ範囲）。**残既存制約**: 仮引数・グローバル変数は long long/plain char/long double
  すべてで _Generic 型区別ビットを伝播しない（params/globals に is_long_long 等が無い共通の既存ギャップ。
  long double 固有でないため今回は対象外）。

### このセッション（続き6）: 大きめプログラムの探索で新規 1 件
最適化が絡む大きめプログラム・libc 連携・レジスタ圧迫を狙って差分探索。大半 green（行列積・qsort・
malloc/tree・hashtable・state machine・Duff's device・多数ローカル/fp 等は全一致）。新規 1 件:
- **グローバル struct の char 配列メンバの文字列初期化**（8c8ce2a）global_struct_char_array_member。
  `struct S{char name[8];} g={"main"}` が char[] メンバを char* と取り違え `.quad .LC0`(ラベルアドレス) を
  8 バイトに格納し name がポインタ値に化けた。psx_gbrace_flat に「char 配列メンバ (tag 無し・要素 1 バイト・
  array_len>0) の文字列を array_len バイトへ展開」する case を追加（多次元 char 配列の行展開と同じ機構、
  要素サイズは tag_member_info.type_size から）。scalar/配列メンバ併存・escape・短い文字列・ローカルを網羅。
  **未対応（別 slot 相互作用、次セッション候補）**: (a) char[] メンバの後に char* メンバが続く形
  `struct{char buf[4];char*p;}` で p が .quad 0 に、(b) 2 次元 char メンバ `char rows[2][4]`、
  (c) struct 配列内の char メンバ `struct{char tag[4];int n;} g[2]`。いずれも基本の char[] メンバ展開
  (8c8ce2a) は効くが、後続メンバ/ネストの slot マッピングで値が壊れる。

### このセッション末の網羅探索（~84 probe、すべて clang 一致 = 新規バグなし）
2D ポインタ配列を全象限修正した後、手薄だった領域を網羅探索したが miscompile はゼロ。
**再探索不要の領域は bug_coverage.md「チェック済みだが miscompile でなかった領域」末尾（2026-06-22 節）に
索引済み**: 複合代入・ビット幅・enum×bitfield・整数昇格/変換・ABI(>8引数/混在/スピル)・三項/論理・
ポインタ/配列境界・VLA/typedef/宣言子・集約初期化・グローバル初期化・文字列/文字エスケープ・除算剰余符号・
リテラル各種・プリプロセッサ stringize 等。式・制御フロー・ABI・数値変換は堅牢で、バグは依然「宣言子・型の
複雑な組合せ」に集中する傾向。

### 発見したが未修正（次セッションの着手候補。再現確認済み）
1. **グローバル struct の char 配列メンバ + 後続/ネストの slot 相互作用** — **4 形 (a)(b)(c)(d) すべて消化済み**:
   - (a) char[] メンバの後に char* メンバ → global_struct_member_after_fp_decl (続き7)。
   - (b) 2 次元 char メンバ → global_struct_2d_char_array_member (続き8)。
   - (c) struct 配列内の char メンバ → global_struct_array_char_member (続き9)。
   - (d) 3 次元以上の char メンバ → global_struct_3d_char_array_member (続き10)。
2. **ローカル (非 static) struct の char 配列メンバ** — **全形消化済み**:
   - 1D ローカル: 元から動作。
   - 2D ローカル: local_struct_2d_char_array_member (続き11)。
   - 3D ローカル: local_struct_3d_char_array_member (続き13)。
3. **配列メンバへの brace elision** — multidim_char_member_brace_elision (続き12) で global+local
   両方を 2D/3D 共に消化済み。
4. **多次元配列メンバへのネスト designator** — global_multidim_array_nested_designator (続き13) で
   2D/3D int を消化。
5. **グローバル struct の fp 配列メンバ** — global_struct_fp_array_member (続き14) で 1D float /
   2D double / スカラ混在を消化。
6. **タグ shadowing 基本形** — tag_shadowing_block_scope (続き15) で同スコープ宣言+参照のケースを
   消化。
7. **タグ shadowing 応用形** — tag_shadowing_advanced (続き16) でネスト 2 段 shadow と内側からの
   外側グローバル変数参照を消化 (lvar_t/global_var_t/node_mem_t に tag_scope_depth_p1 を追加し
   宣言時 scope を覚える機構)。
- それ以外: HANDOFF 列挙の既知未対応はすべて消化済み。上記網羅探索領域も再探索不要。**未探索の角度**
  （複数 TU リンク、ライブラリ関数との相互作用、ランダム生成ファズ）から新規 miscompile を炙り出す。
  索引は `docs/differential_testing/bug_coverage.md`。

### このセッション中の注意（プロセス）
- ヘッダ（token.h / semantic_ctx.h / parser_public.h / node_utils.h 等）を変更すると **増分ビルドが
  依存を取りこぼし古い .o を使う**ことがあり、`make test` が偽の `test=2` を出した（2回）。
  ヘッダ変更時は `make clean && make` で確認すること（コードの問題ではない）。

## 2026-06-21 セッションの修正（bug_coverage.md の ⬜ 未着手をすべて消化）
現在 `make test` = **984 E2E + unit/parser/preprocess/fuzz/IR すべて green**、回帰 fixture は
`test/fixtures/probes_found_bugs/` 登録済み。`docs/differential_testing/bug_coverage.md` を
索引として更新済み。テーマ別:

- **ファイルスコープ static の内部リンケージ**（72b23c2）。`static` 変数/関数/関数内 static を
  `.global` で出し、無初期化 static を `.comm`(外部 common) にしていたため別 TU の同名 static と
  duplicate symbol 衝突/共有していた (C11 6.2.2p3 違反)。`is_static` を global_var_t / ir_func_t /
  node_func_t に追加し parser から伝播、codegen で `.global` 抑制 + 無初期化を `.zerofill __bss`。
  fixture: static_internal_linkage。**複数 TU の差分テストで発見**。
- **クロス TU (複数ファイル) E2E ケース種別を追加**（39e4f37）。`test_e2e.c` の `link2_cases[]` で
  2 ファイルを同じ接頭辞で namespace して category binary に一緒にリンクし、別 TU の同名シンボル
  衝突を検査できる。fixture: static_internal_linkage_xtu_{main,other}。
- **printf 浮動小数書式は green**（ab4355b、docs のみ）。`%f/%g/%e/%E`・精度/幅/フラグ・偶数丸め・
  Inf/NaN・float→double 可変長昇格・snprintf を stdout 比較。書式は libc 任せで ag_c は引数を
  正しく渡しており全一致。
- **`_Generic` の複雑な派生型照合**（5632b86 局所変数 / 1cbf0cb cast / cc1e788 グローバル変数）。
  関数ポインタの引数リスト (`int(*)(int)` vs `int(*)(int,int)`) や深いネスト型
  (`int(*(*)(void))[3]`) を generic_type_t の構造的フィールドでは区別できなかった。型を
  **正規化トークン文字列** (`psx_serialize_decl_type_tokens`) にして strcmp 照合する。control が
  局所変数のときは宣言時に名前で記録 (decl.c の副テーブル)、グローバルは永続表、cast は cast 経路
  で type_sig を採用。fixture: generic_complex_derived_type{,_global}。
- **ストリーミング前方先読み境界バグ**（1d584e8、既存バグ）。`_Generic` の型照合はカーソルを
  進めず `t->next` で型全体を先読みするが、ストリーミング生成器の materialize 窓を越えると
  `t->next==NULL` を踏み有効な型を誤却下 (E2006 ': 必要')。深い先読みの直前に
  `tk_ensure_lookahead()`（プリプロセッサが登録するフック経由で `pps_refill`、parser↔preprocess は
  疎結合維持）で窓を満たす。fixture: generic_streaming_lookahead。詳細は bug_coverage.md。

## 2026-06-18 セッションの修正（11 件、7e39081 まで）
現在 `make test` = **914 E2E + unit/parser/preprocess/fuzz/IR すべて green**、各修正は
ASAN クリーン、`test/fixtures/probes_found_bugs/` に回帰 fixture 登録済み。
- 符号付き short/char キャストの符号拡張（インライン使用で sign-extend、f9f153f）。
  fixture: cast_short_char_sign_extend。
- 2D 配列の行のポインタ算術 `*(m[i]+k)` / `*(p+k)+j`（node_is_ptr_for_arith で行を
  ポインタ判定、build_node_deref で 8B 以下の行も崩壊、1fd9eb1）。array_row_decay_pointer_arith。
- 3D 以上の中間行のポインタ算術 `*(*(t[i]+j)+k)`（多段ストライド伝播、47975d4）。
  array_row_decay_3d_pointer_arith。**変則 `*(t+1)[0]` 形は未対応（既存）**。
- 関数ポインタ変数の float/double 戻り値（callee の pointee_fp_kind を funcall へ、0b980b0）。
  funcptr_fp_return。**配列要素/struct メンバ/global の funcptr 戻り、int→fp 引数昇格は未対応**。
- static struct/union 局所の永続化（global lowering + stmt.c の static フラグ復元、8167e8e）。
  static_local_struct_persist。**インライン定義の匿名タグは未対応（codegen でタグ消失）**。
- i32 比較を 32bit 幅（w レジスタ）で行う + sub-int 戻り値の切り詰め（42b9d54）。
  `int f(int x){return x-1;} f(0)!=-1` 等の負値インライン比較を是正。戻り型 unsigned 追跡
  （ret_is_unsigned）と funcall への伝播も追加。int_cmp_width_and_subint_return。
- unsigned char/short 戻り値のゼロ拡張（前項の回帰修正、1b5e1df）。
- 匿名 struct/union メンバ昇格時の fp_kind/is_bool/is_unsigned/outer_stride 伝播（f4bf0bd）。
  `struct { union { int n; float f; }; }` の `s.f` 整数化を是正。anon_member_fp_unsigned_promote。
- グローバルポインタ配列 `&data[n]` / `data+n` 初期化（resolve_global_addr_init + codegen で
  `_sym+off`、138cd70）。global_ptr_array_addr_init。
- グローバル集約の `.member[idx]` / `.member.sub` designator チェーン（1e843b4）。
  global_designator_member_index。
- ローカル designator の struct leaf を `{...}` で brace 初期化（7e39081）。
  local_designator_aggregate_leaf。**union leaf は未対応（union 配列要素 brace init が別途未対応）**。

手法・再現手順は冒頭の「作業のやり方」を見ること。下記「既知の未対応」も更新済み。

## 作業のやり方（再現手順）
- 差分テストヘルパ（**正本は repo 追跡の `scripts/agc_diff_test.sh`**）:
  - `scripts/agc_diff_test.sh <file.c>` — repo ルート（スクリプト位置から自動導出、CWD 非依存）で
    ag_c を実行。`include/stdarg.h` 等の同梱ヘッダ (`#include`) を解決できる。src は `cd $ROOT`
    するので **絶対パス推奨**。`ag_c → .s → clang でアセンブル → 実行` と `clang -w -I include 直接`
    を実行し、**exit code・stdout・正規化した stderr の 3 つを比較**して
    `OK`/`MISMATCH`/`AGC_COMPILE_FAIL` を表示する。
  - **【重要】exit code だけ比較する罠**: assert 失敗はすべて exit 134 に潰れるため、
    exit code だけ比較すると ag_c と clang が**別々の assert で abort**しても両方 134 で「OK」と
    誤表示する。これを避けるため stdout と stderr も比較する。stderr は assert メッセージの
    `file <path>,` 部分だけ正規化 (ag_c=basename / clang=フルパスの差を吸収)、残りの式・関数・
    **行番号**を比較する。挙動が同じなら 3 つすべて一致するはず。MISMATCH 時は
    `[rc stdout stderr]` のどれが食い違ったかと差分を表示する。printf 系の stdout 差も同じツールで
    検出できる（別スクリプト不要）。
  - **クロス TU (複数ファイル)**: 各 .c を ag_c で .s 化 → clang で個別アセンブル → 一緒にリンク
    し、`clang -w -I include` の直接ビルドと比較する使い捨てスクリプトを `/tmp` に作って使う
    （src は絶対パスで渡すこと）。恒久回帰は `test_e2e.c` の `link2_cases[]`（2 ファイルを同接頭辞で
    namespace して category binary にリンク）に追加する。
  - 使い捨ての `/tmp/*.sh` を作るときは `Write` ツールで作る（`echo >` は使わない方針）。
- ag_c はアセンブリを **stdout** に出力する (`./build/ag_c foo.c > foo.s`)。`-o` や `-I` の
  コマンドラインフラグは無い。include 検索は CWD 相対の `include/`（preprocess.c の
  `k_include_search_roots`）。
- ビルド: `make`（日本語診断 `-DDIAG_LANG_JA`）。Edit 直後に mtime 粒度で再ビルドされない
  ことがあるので `touch <file>` してから `make`。
- 全テスト: `make test`（E2E + parser/preprocess 単体 + fuzz + IR）。E2E は
  `test/test_e2e.c` に登録、fixture は `test/fixtures/probes_found_bugs/`。
- **メモリ系バグは ASAN で特定**するのが有効だった（間欠 SIGSEGV を 1 回で再現）。
  zsh は単語分割しないので `${=SRCS}` を使う:
  ```
  clang -std=c11 -g -O0 -fsanitize=address -DDIAG_LANG_JA -Isrc \
    src/*.c src/config/*.c src/arch/*.c src/tokenizer/*.c src/parser/*.c \
    src/preprocess/*.c src/ir/*.c src/diag/diag.c src/diag/error_catalog.c \
    src/diag/ui_texts.c src/diag/warning_catalog.c src/diag/messages_ja.c -o /tmp/ag_c_asan
  ```
  注意: グロブ (`src/*.c`) は **コマンドラインに直書き** で展開する。`SRCS="src/*.c ..."` を
  `${=SRCS}` で渡すとグロブ展開されず `no such file` になる（単語分割しかしないため）。
  messages_en.c / messages_all.c は messages_ja.c と重複定義になるので入れない。
- 新規 fixture の登録は `test/test_e2e.c` に 2 系統:
  - `CASE_INT_FILE`（exit code で判定、慣習で 42）:
    `{"probes", "<name>", CASE_INT_FILE, "...<name>.c", 42, 0}`。
  - `CASE_ASSERT_FILE`（fixture 内の `assert()` が期待を自己記述、成功で main が 0 を返す。今回の
    `_Generic`/linkage fixture はこちら）: `{"probes", "<name>", CASE_ASSERT_FILE, "...<name>.c", 0, 0}`。
    `#include <assert.h>` と `int main(void){ assert(...); return 0; }` 形式。
  - 回帰検証は「修正を一時的に外して fixture が落ちる」ことまで確認する（バグ注入 → make / 該当
    fixture 実行で失敗を確認 → 復元）。`touch` してから `make`（mtime 粒度の取りこぼし回避）。
- **【重要】探索が green だった領域も必ず記録する**。差分テストして miscompile が無かった（clang 一致）
  領域は、`docs/differential_testing/bug_coverage.md` の「チェック済みだが miscompile でなかった領域
  （再探索不要）」にテーマ別で追記する（日付節を作る）。記録しないと次セッションが同じ領域を再探索して
  しまう。バグ修正（🔧）だけでなく「探索したが堅牢だった」ことも成果として残すこと。

## アーキテクチャ早見
tokenizer → preprocess → parser（`src/parser/`）→ IR builder（`src/ir/ir_builder.c`）→
ARM64 codegen（`src/arch/arm64_apple*.c`）。ターゲットは Apple Silicon ARM64。

- ノード型情報のアクセサ: `src/parser/node_utils.c`（`psx_node_type_size` /
  `psx_node_is_pointer` / `psx_node_is_unsigned` / `psx_node_pointer_qual_levels` /
  `psx_node_base_deref_size` / `psx_node_set_unsigned` など）。`node->kind` で分岐し、
  ND_LVAR は `mem`、ND_GVAR/DEREF/ASSIGN は `node_mem_t`、その他は base を読む（フィールド
  が分散しているので **読み書きでフィールドを一致させること**）。
- struct メンバ情報は `tag_member_t`（semantic_ctx.c）/ `tag_member_info_t`（header）。
  メンバ属性（fp_kind / is_bool / is_unsigned / outer_stride）は setter/getter を介して
  hash table に保存し、`psx_ctx_get/find_tag_member_info` で取得。新属性を足すときは
  ① 両構造体にフィールド ② setter/getter ③ struct_layout.c で設定 ④ build_member_deref_node
  で deref ノードへ伝播、の 4 箇所を揃える（`is_bool` を雛形にできる）。
- struct 値の ABI（簡略版）: 引数は**ポインタ渡し**（callee が memcpy）、>16B 戻り値は
  x8 経由の ret_area。`build_node_funcall` は値文脈で >8B struct 戻り値の ret_area を確保
  しアドレスを返す。
- グローバル初期化は `psx_parse_global_brace_init_flat`（parser.c）が **フラット index** に
  値を書き、`emit_global_struct_init`（arm64_apple.c）が struct レイアウトに沿って出力する
  （入れ子 struct は再帰展開）。

## 直近セッションの要約（db98d34〜e0b5190、22 件）
現在 `make test` = **904 E2E + unit/parser/preprocess/fuzz/IR すべて green**、各修正は ASAN
クリーン、`test/fixtures/probes_found_bugs/` に回帰 fixture 登録済み。テーマ別の内訳:
- 型幅/キャスト: sub-int の (int)/(unsigned) 符号（1317698）、(int)/(long) の 32/64bit
  切り詰め・拡張（1c8e358, 6070c7e, 8ddafa6）、long リテラルの i64 型付け（365d8c0）、
  インライン `*(int*)ptr` の pointee サイズ（d44947f）
- ポインタ/配列: struct ポインタ配列メンバ（db98d34）、多次元配列の行 decay（a2a8328）、
  struct/union ポインタ算術スケール（b6a42ec）、配列へのポインタ（>4B struct, 局所2D）
  （10072cc, 5a47279）
- 集約: ネスト designator の `[idx]`（aadf3b7）、struct 配列メンバ brace init（1684a8c）、
  _Bool 初期化子正規化（5b3d592）
- struct ABI: 3/5/6/7B struct の値渡し/返し（050e1bf）
- **float/double**（codegen の穴を集中的に）: 配列メンバ access/init（10b9748）、
  多次元 subscript の fp load（10d291d）、ブール条件分岐（ec96e30）、`&&`/`||`（87a24a1）、
  static ローカル float init（f873777）
- 型機能: `_Alignof` の集約アラインメント（3e8a4d1）、`_Generic` の文字列/long リテラル（e0b5190）

詳細は下記テーマ別セクション参照。手法・再現手順は冒頭の「作業のやり方」を見ること。

## このセッションで修正した内容（コミット c1a8f83 以降）
テーマごと。すべて `make test` green、該当パスは ASAN クリーン、fixture 登録済み。

### 64bit 幅 / long
- ポインタ三項の null 定数分岐が 4 バイトしか書かず garbage（d150e3b）
- ternary / binop / リテラル / 戻り値 / 引数の 64bit 幅取りこぼし（4329e49, 56e96fe,
  a9a965c, fcbd1e4）— long の値が下位 32bit に化ける一連
- 入れ子三項の long 分岐（fcbd1e4 に含む）

### ポインタ / subscript（`build_subscript_deref` 周辺の「配列 vs 多段ポインタ」取り違え）
- struct ポインタ局所変数の type_size が 8 でなく pointee サイズで、`p=p->next` が 16B
  struct コピー扱いになり隣接スタック破壊（f0b096c）
- 単段ポインタ subscript 結果がスカラなのに pointer 扱い（6fe7dce）
- 多段ポインタ subscript の pql 減算（d0ff96c）/ base_deref_size 伝播（8e937e0）
- `long *a` 仮引数 subscript / 呼び出し側 long 戻り値幅（a9a965c）

### 集約初期化（C11 6.7.9）— ローカル & グローバル
- 多次元配列メンバ `int a[2][2]`（4b92768）
- 重複 designator 後勝ち（4a5942d、**ユーザー確認のうえ C11 準拠を選択**）
- designator 後の位置指定継続（df23e17）
- ネスト struct メンバ brace 省略 `{1,2,3}`（17edc06）
- struct 配列フラット展開 / 配列メンバ部分充填→designator（b225c4c）
- グローバル入れ子 struct 初期化の data 出力（5be6940）
- グローバル designator の slot 計算（入れ子 struct / struct 配列 `[N]=`）（1b79768）
- 関数ポインタ配列 struct メンバの brace 初期化（7e6912c）
- **struct 配列メンバ** `struct P pts[N]` の要素 brace 初期化（1684a8c）。ローカルは
  parse_member_initializer が要素を parse_struct_initializer へ委譲、グローバルは
  emit_global_struct_members_rec が各要素 struct を再帰展開。positional/designated/
  部分ゼロfill 対応。差分テストで発見。**未対応**: グローバルでネストメンバ内の配列添字
  designator `{.items={[2]={.a=7}}}` はグローバル flat パーサの別制約（h83k）。
- メンバパス途中に配列添字を含む designator `.m.x[1].b` / `.arr[i].f`（aadf3b7）。
  `consume_nested_designator_and_build_assign` を `.member` と `[idx]` の任意連鎖を
  辿るよう一般化し、indexed 専用ヘルパを統合（scalar/union/配列要素・境界チェック維持）。

### struct ポインタ配列メンバ
- `struct N *arr[N]` メンバの subscript 結果が struct ポインタと認識されず
  `h.arr[i]->v` が E3005（db98d34）。`build_member_deref_node` がポインタ配列メンバに
  pql=1 / base_deref_size=要素 pointee サイズ を立て、`build_subscript_deref` の
  「要素がポインタ」分岐に乗せて is_tag_pointer を伝播（ローカル `T *arr[N]` と同じ表現）。

### unsigned 符号拡張（load / 比較の signedness）— **全経路を網羅**
- `(int)`/`(unsigned)` キャストが operand の符号を更新せず比較が誤符号（5b474f7）。
  ※ binop ノード（シフト）の is_unsigned は LSR/ASR を兼ねるので終端値ノードのみ更新。
- unsigned グローバル / struct・union メンバ（70d91ab）、long/char/short 幅（3a61e09）、
  配列要素 / ポインタ deref（14ca899）、typedef したグローバル（8bd7ec8）
- **char/short を (int)/(signed)/(unsigned) へ明示キャスト**すると operand の
  is_unsigned 上書きが load 拡張 (ldrsh/ldrh) を変え値が化けていた（1317698）。
  `(unsigned)(short)-1`→65535、`(int)(unsigned short)0xFFFF`→-1。sub-int は load
  符号性を保ち、(unsigned) は & 0xffffffff で 32bit unsigned へ折り返す（符号混在の
  インライン比較/除算も是正）。差分テストで発見。

### 多次元配列の decay（行→ポインタ）
- 多次元配列の途中次元（行）を**値**として使う（`int *q=m[0]`、`*(*(m+1)+2)`、`**m`）と
  int* へ崩壊せず値ロードして garbage（a2a8328）。① build_node_deref の崩壊判定が
  is_pointer=1 を要求していたのを `deref_size>0 && type_size>8` に緩和（struct 値は
  deref_size=0 で除外）。② 通常多次元配列は ND_ADDR(deref_size=行, inner_deref_size=要素)
  表現で、build_unary_deref_node が `*m`/`*(m+k)` の内側ストライドを引き継いでいなかった。
  ND_ADD を辿り 1 段シフト。3D・関数引数 `int(*)[N]`・グローバル 2D も対応。差分テストで発見。

### _Bool 初期化子の正規化
- `_Bool` は 0/1 を保持すべき（C11 6.3.1.2、非ゼロ→1）だが、スカラ変数 init のみ正規化され
  配列・struct メンバ・グローバルの各 init 経路は生値を格納（`_Bool f[]={5}` が 5）（5b3d592）。
  全 init サイトで (value!=0) を適用: ローカル配列 / ローカル struct スカラ・配列メンバ /
  グローバル配列（global_var_t に elem_is_bool 追加）/ グローバル struct メンバ（emit で
  tag member is_bool 参照）。`bool_normalize_if` ヘルパ。差分テストで発見。

### (int)/(signed)/(unsigned) による long の 32bit 切り詰め
- `(int)long` が 32bit に切り詰められず、代入/戻り値では store 幅で偶然合うがインライン比較/
  演算で 64bit のまま使われ `(int)0x100000000L == 0` や `(int)long_var == 0` が偽（1c8e358）。
  定数は値を切り詰め、long 値は `(x<<32)>>32`（signed=算術 / unsigned=論理シフト）で低32bitを
  64bitへ拡張。sub-int・ポインタ→int は対象外。差分テストで発見。

### _Generic の制御式型推定（文字列・long リテラル）
- `_Generic("str", char*:...)` が文字列を char* に decay 認識せず（ptr_deref_size 未設定）default に、
  `_Generic(42L, long:...)` が long リテラルを int 扱い（scalar_size=4）で long にマッチせず（e0b5190）。
  文字列は ptr_deref_size=文字幅、NUM は int_is_long で scalar_size=8 に。差分テストで発見。

### _Alignof が集約型でサイズを返す
- `_Alignof` が sizeof 実装で、基本型は size==align で偶然合うが struct/union/配列で誤り
  （`_Alignof(struct{int,int})`=8（正4）、`_Alignof(int[10])`=40（正4）、`_Alignas(16)` 含む
  struct=32（正16））（3e8a4d1）。tag テーブルに agg_align を保存（layout が pending 経由で
  define に渡す）。_Alignof モードでは struct の align を返し、配列は要素数を掛けない。
  sizeof は不変。旧挙動を符号化していた E2E fixture 2 件・parser 単体 2 件も修正。差分テストで発見。

### static ローカル float/double の初期化
- `static float t = 0.5f;` が `.long 0` で出力され値が 0 になっていた（f873777）。
  try_lower_static_local_scalar が NUM の整数 `->val` のみ読み、float リテラル値 `->fval` を
  無視していた。fp なら fval/fp_kind を gv に伝播。差分テストで発見。

### float をブール条件/論理演算で使う（codegen）
- `f ? a : b` / `if(f)` / `while(f)` / `f && g` / `f || g` で float/double を真偽値として使うと、
  レジスタ圧で fp が spill されたとき codegen が 4B float を 8B 整数 load して上位 garbage を拾い、
  0.0 が真と誤判定（ec96e30 で条件分岐、87a24a1 で `&&`/`||`）。`emit_truthiness` ヘルパ（fp は
  `!= 0.0` の IR_FNE、整数は IR_NE）に統一し、emit_br_cond と build_node_logand_or 右辺で使用。
  差分テストで発見。

### 多次元 float/double 配列の subscript
- `float m[2][3]` の `m[i][j]` が整数 load で読まれていた（10d291d）。build_subscript_deref が
  float 配列 subscript 結果の fp_kind を常に base.fp_kind に載せていたため、多次元 1 段目（まだ行）
  の結果が float 値扱いになり、次段 subscript が pointee の fp 種別を失い整数 load。中間（行）は
  pointee_fp_kind、最終要素のみ base.fp_kind に（is_bool と同じ分岐）。`es > inner_ds` で多次元中間
  かを判定（`float *a` 仮引数は inner_ds=elem が立つので es 比較で区別）。差分テストで発見。

### struct の float/double 配列メンバ
- `struct S{ float v[N]; }` の `s.v[i]` アクセスと brace 初期化が整数扱いで値が化けた（10b9748）。
  アクセス: build_member_deref_node が配列メンバの fp_kind を base.fp_kind に入れていたのを
  pointee_fp_kind に（is_bool と同じ分岐、subscript 結果を fp load）。初期化: parse_member_initializer
  に member_fp_kind を通し要素 store を fp store に（build_member_array_elem_assign_node）。差分テストで発見。

### 局所「2次元配列へのポインタ」の mid_stride
- 局所 `T (*p)[N][M]` が mid_stride 未設定で `p[i][j]` が誤スケール（5a47279）。局所宣言経路は
  paren の `[N][M]` を積(outer_stride)としてのみ扱い第2 subscript 用 stride を立てていなかった。
  paren-array の先頭次元・次元数を捕捉し mid_stride=(積/先頭次元)*elem を設定。int/struct 共通。
  引数版は元から動作。差分テストで発見。

### 配列へのポインタ（要素が >4B struct）
- `struct T (*p)[N]` の subscript が要素 struct >4B のとき壊れた（10072cc）。局所は
  lvar_is_pointer が `size>elem_size`（8>8 が偽）でポインタ非認識（4B struct は 8>4 で動作）、
  引数は struct ポインタ仮引数ブランチが `[N]` を無視し `struct T *` 扱い（行を跨げない）。
  局所は `outer_stride>0 && size==8` でポインタ認識、引数は is_tag_pointer クリア＋outer_stride
  設定。`long (*g)[N]` 引数の pql=1 化も抑止。差分テストで発見。

### long/long long リテラルの 64bit 型付け
- 値が 32bit に収まる long サフィックス付きリテラル（`2L` 等）が i32 扱いになり、`u * 2L`
  （u は unsigned >2^31）が 32bit 演算で wrap（365d8c0）。tokenizer は int_size に L を記録
  していたが parser が node に伝えていなかった。node_num_t に `int_is_long` を追加し
  parse_num_literal で伝播、build_node_num が long リテラルを i64 生成。is_unsigned サフィックスも
  node へ伝播。通常 int リテラルは値ベース型付けのまま（`w * 2u` の wrap は不変）。差分テストで発見。

### (long)unsigned_int の zero-extend（arithmetic）
- `(long)u + (long)u`（u は unsigned int で値 >2^31、合計が 2^32 超）が 32bit でラップ（8ddafa6）。
  `(long)` が type_size を 8 に広げない no-op のため二項演算 result_ty が I32 になり、符号なし
  2^32 ラップマスク（[ir_builder.c]~1230）で切り詰められていた。`coerce_to_type` は常に SEXT で
  unsigned widen に乗れないので、node_mem_t に `widen_zext_i64` フラグを追加し、`(long)unsigned`
  (8B未満) を ND_PTR_CAST(widen_zext_i64) でラップ → `build_node_ptr_cast` が IR_ZEXT を明示挿入。
  signed の (long) は SEXT で従来通り。差分テストで発見。

### インラインのポインタキャスト deref
- `*(int*)(cp+4)`（char* を別要素型へキャストして即 deref/添字）が、キャストで pointee
  サイズを更新せず元の char サイズ(1)でロードしていた（d44947f）。一旦変数 `int *p=...` に
  入れると正しく動いた。スカラ整数型への単段ポインタキャストを ND_PTR_CAST で deref_size
  更新。operand がポインタのときのみ（`(int*)0` 等の null 定数は ND_NUM のまま）、多段は除外。
  差分テストで発見。

### 小さい struct の値渡し/値返し ABI（サイズ 3/5/6/7）
- サイズが 1/2/4/8 でない struct/union（3/5/6/7B）を値渡し/値返しすると、先頭メンバ幅の
  スカラとして 1 レジスタで運ばれ先頭メンバしか残らなかった（`{char;short;uchar}` 6B が
  1B 扱い）（050e1bf）。簡略 ABI が至る所で `>8` を間接（memcpy/x8 ret_area）境界にしていた
  のが原因。`cg_size_needs_indirect_struct`（1/2/4/8 以外を間接）を追加し、引数渡し/受け取り・
  struct 代入・戻り値設定/受け取り・struct 戻り値呼出の内側 arg ループ・複合リテラル引数の
  全経路に適用。1/2/4/8B は従来通りレジスタ値渡し。差分テストで発見。

### struct/union ポインタ算術
- `&s[i]-&s[j]` / `sp+n` / `n+sp` / `sp-n` が要素サイズで割られず/掛けられず byte 単位
  だった（b6a42ec）。struct タグポインタは is_pointer でなく is_tag_pointer で表現され、
  `add()` の `psx_node_is_pointer` 判定が偽を返していた。`node_is_ptr_for_arith`
  （タグポインタも認識）を追加。複合 `+=`/`-=` は別経路で元から正常。差分テストで発見。

### long bitfield
- `unsigned long a:40` 等 >32bit / 32bit ユニットを跨ぐ bitfield（040da11）。storage を
  8 バイト許可 + 64bit load/store + マスク定数をレジスタ展開。

## 既知の未対応 / 次の着手候補
（差分テストで新しい miscompile / コンパイルエラーを炙り出すところから再開する。
2026-06-18 セッションで上記 11 件、2026-06-21 セッションでさらに数件を修正済み。下記は現時点で
残る既知の未対応。直近のプローブ（型×文脈の総当たり寄り）はかなり green で、発見コストが
上がっている。）

- **現状の索引は `docs/differential_testing/bug_coverage.md`**。チェック済み領域・⚠️/🔧 状態・
  既存バグ・「miscompile でなかった」領域をテーマ別に集約している。2026-06-21 時点で同ファイルの
  ⬜ 未着手候補（複数 TU/extern、printf 書式、`_Generic` 複雑派生型）は全消化。再開時はまず
  これを見て未探索の「型 × 宣言経路 × 使用文脈」の組合せを選ぶ。下記は古い既知の未対応の抜粋。

- **関数ポインタ FP 戻り値の残り**（funcptr 変数は 0b980b0 で対応済み）。callee が配列要素
  `ops[i]` / struct メンバ `s.f` / グローバル関数ポインタの場合は戻り型 fp_kind が funcall へ
  伝播せず戻り値を x0 で読む。subscript/メンバ/global_var_t への配線が要る。また間接呼び出しの
  **int→fp 引数昇格**（`double(*p)(double); p(4)` の整数リテラル）は仮引数型保存が必要で未対応。

- **codegen の符号性/幅の深い穴**。`unsigned long` / `unsigned char` 戻りの符号性は plain
  `unsigned` のみ追跡（ret_token_kind が TK_LONG/TK_CHAR に潰れる）。混在幅の比較で片側が
  i32・片側 i64 のケースは 64bit 比較になる（gen_inst_int_cmp は両 i32 のときだけ 32bit）。

- **多次元・ポインタの変則形**。`*(t+1)[0]`（pointer-to-2D-array に subscript+deref 混在）は
  コミット前から SIGSEGV（47975d4 の標準 3D 行算術とは別経路）。

- **union 集約初期化の穴**。`union U arr[2]={[1]={.n=5}}` / `.u[1]={...}`（union 配列要素の
  brace init）が誤値。local designator の union leaf は 7e39081 で意図的に E3064 のまま除外。

- グローバルのネスト brace 配列添字 `struct O o={.items={[2]={.a=7}}}`（`{[2]=...}` 形）は
  グローバル flat パーサの制約で E3064。`.items[2].a=` 形（designator チェーン）は 1e843b4 で対応済み。

- 同梱ヘッダに `complex.h` が無く `#include <complex.h>` が E1034。`_Complex` の言語機能自体は
  動く（creal/cimag 等のライブラリ関数を使わなければ可）。

## バグではない（仕様 / 既知の差異、追わない）
- statement expression `({...})`（GNU 拡張、非標準。プロジェクトは C11 準拠）。
- 過剰初期化子 `struct S s={{1,2},{3,4}}`（メンバ1個に2グループ）等は clang が警告して
  無視するが ag_c は E3064。意図的に厳格（miscompile ではない）。
- `s07.c`（深さ 10 万の再帰）が SIGSEGV。各フレームのスタック使用量が大きいことによる
  スタックオーバーフローで、誤コンパイルではない（深さ 100 では正しく動く）。
- 評価順序が未規定/UB のもの（`a[i++]=i` 等）。

## メモ
- 「型バリエーション × 宣言経路 × 使用文脈」の組み合わせで取りこぼしが多発する傾向。
  例: unsigned を直しても int だけ／local だけ／scalar だけ、になりがち。1 つ直したら
  long/char/short・global・typedef・配列要素・ポインタ deref まで広げて確認すると良い。
- 集約初期化・struct 値受け渡しは「ノード種別を特別扱いする分岐」が多く、新しいノード形
  （compound literal=ND_COMMA、subscript=ND_DEREF、funcall 戻り値=ND_FUNCALL）が漏れがち。
