# implementation_plan.md

ag_c リファクタリング詳細計画。全 3 Phase、計 9 Step group、約 31 commit。

## 全体方針

| 項目 | 値 |
|---|---|
| 構成 | Phase A → B → C |
| 1 Step | 1 commit (実装 → ビルド → make test → probe sweep → commit) |
| 各 commit 厳守 | `make test` で 820/820、probe diff で diverged=0 |
| 機能変更 | ゼロ (e2e 期待値・IR 表現・asm 出力すべて据置) |

## 期待される結果

| 指標 | 着手前 | 目標 |
|---|---|---|
| 最大関数行数 | 1055 (build_expr) | < 300 |
| `psx_decl_parse_declaration_after_type_ex` | 364 行 | < 150 |
| tag_member API 数 | 5 | 1 |
| `var->tag_kind = ` 重複 | 9+ 箇所 | 0 |
| ast.h | 308 行 (混在) | AST < 200, symtab < 150 |
| IR の parser/internal include | あり | なし |
| 総 commit 数 | - | 約 31 + docs |

---

## Phase A: 保守性改善 (低リスク, 機能不変)

純粋な抽出のみ。動作変更ゼロ。820 e2e 通過は高確度で維持。

### A1: tag_member API 統合

**目的**: 5 つに分裂した取得 API を 1 つに集約

**現状の API**:
- `psx_ctx_get_tag_member_at` (offset, type_size, deref_size, array_len, tag_kind, tag_name, tag_len, is_tag_pointer)
- `psx_ctx_get_tag_member_bf` (bit_width, bit_offset, bit_is_signed)
- `psx_ctx_get_tag_member_fp_kind` (fp_kind)
- `psx_ctx_get_tag_member_is_bool` (is_bool)
- `psx_ctx_get_tag_member_count` (count)

**対象ファイル**:
- `src/parser/semantic_ctx.c`
- `src/parser/internal/semantic_ctx.h`
- `src/parser/decl.c` (呼び出し側)
- `src/parser/expr.c` (呼び出し側)

**設計**:
```c
typedef struct {
  char *name; int len;
  int offset; int type_size; int deref_size; int array_len;
  token_kind_t tag_kind; char *tag_name; int tag_len; int is_tag_pointer;
  int bit_width; int bit_offset; int bit_is_signed;
  tk_float_kind_t fp_kind;
  int is_bool;
} tag_member_info_t;

bool psx_ctx_get_tag_member_info(token_kind_t kind, char *name, int len, int index,
                                  tag_member_info_t *out);
```

既存 5 API は内部実装として残し、新 API でラップ。呼び出し側を段階移行 → 最後に旧 API 削除。

**Step**:
- **A1-1**: `tag_member_info_t` 構造体と `psx_ctx_get_tag_member_info` 新規追加。既存 API は無変更
- **A1-2**: `src/parser/decl.c` の呼び出し移行 (主に `tag_find_member`, `tag_get_member_at`)
- **A1-3**: `src/parser/expr.c` と `src/ir/ir_builder.c` の呼び出し移行
- **A1-4**: 旧 5 API のうち、unused になったもの削除

**完了条件**:
- tag_member 関連シンボル 5 → 1 (`*_count` は残す)
- call site の `(tag_kind, tag_name, tag_len)` 引数 3-tuple が 0 個 (info 構造体内に集約)
- 820/820、diverged=0

**リスク**: bitfield/fp_kind/is_bool の漏れ → 既存 API を残したまま並走で probe 検証

**commit 数**: 4 / **時間感**: 3-4h

---

### A2: lvar/var タグ設定ヘルパ化

**目的**: 4-行パターン 9+ 箇所 (実測: decl.c 5 + parser.c 4) を 1 行化

**対象ファイル**:
- `src/parser/decl.c`
- `src/parser/parser.c`
- `src/parser/internal/decl.h`

**設計**:
```c
// internal/decl.h
static inline void psx_decl_set_var_tag(lvar_t *var,
                                         token_kind_t tag_kind, char *tag_name, int tag_len,
                                         int is_tag_pointer) {
  var->tag_kind = tag_kind;
  var->tag_name = tag_name;
  var->tag_len = tag_len;
  var->is_tag_pointer = is_tag_pointer;
}
```
global_var_t 用も同様。`ast.h` の lvar_t/global_var_t は据置 (Phase C1 と独立)。

**Step**:
- **A2-1**: ヘルパ定義追加
- **A2-2**: `src/parser/decl.c` の 5 箇所置換
- **A2-3**: `src/parser/parser.c` の 4 箇所置換

**完了条件**: `grep -c "var->tag_kind = " src/parser/decl.c src/parser/parser.c` = 0、820/820

**リスク**: 極小。grep + コンパイラで全検出可能

**commit 数**: 3 / **時間感**: 1-2h

---

### A3: 型/フラグ伝播ヘルパ化

**目的**: `if (node->fp_kind == TK_FLOAT_KIND_FLOAT) load_ty = IR_TY_F32; else if (...) load_ty = IR_TY_F64;` 等 8+ 箇所重複を集約

**対象ファイル**:
- `src/ir/ir_builder.c`
- `src/parser/expr.c` (build_member_access / build_subscript_deref / build_lvar_or_vla_node)

**設計**:
```c
// ir_builder.c 内部
static ir_type_t ir_type_from_node(node_t *node) {
  if (node->fp_kind == TK_FLOAT_KIND_FLOAT) return IR_TY_F32;
  if (node->fp_kind >= TK_FLOAT_KIND_DOUBLE) return IR_TY_F64;
  // type_size ベースの fallback
  return IR_TY_I32;
}

// parser/expr.c 内部
static void propagate_pointee_flags(node_mem_t *dst, node_mem_t *src) {
  dst->base.fp_kind = src->base.fp_kind;
  dst->is_scalar_ptr_member = src->is_scalar_ptr_member;
  dst->pointee_is_bool = src->pointee_is_bool;
  // ...
}
```

**Step**:
- **A3-1**: `ir_type_from_node` 抽出
- **A3-2**: `build_expr` 内 8 箇所を `ir_type_from_node` に置換
- **A3-3**: `propagate_pointee_flags` 抽出 + parser 側 3 箇所移行

**完了条件**: fp_kind 直接判定 8+ → 1 (ヘルパ内)、820/820

**リスク**: 低。fp_kind 値域 (FLOAT/DOUBLE/LONG_DOUBLE/NONE) 網羅必須

**commit 数**: 3 / **時間感**: 2-3h

---

**Phase A 小計**: 10 commits, 6-9h, regression リスク極小

---

## Phase B: 巨大関数分割 (中リスク)

AST/IR 構造は据置。関数を分割して可読性向上。

### B1: `build_expr` 分割

**目的**: 1055 行の switch を `build_node_<kind>` 30+ 関数に分割。dispatch table 化

**対象ファイル**: `src/ir/ir_builder.c` (必要なら `src/ir/ir_builder_nodes.c` 新設)

**設計**: 一気にやらず 4 ロットに分割
1. lvalue 系 (ND_LVAR, ND_GVAR, ND_DEREF, ND_ADDR, ND_NUM, ND_STRING, ND_ASSIGN, ND_FUNCREF)
2. 算術系 (ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_MOD, ND_SHL, ND_SHR, ND_BITAND, ND_BITOR, ND_BITXOR)
3. 制御系 (ND_TERNARY, ND_LOGAND, ND_LOGOR, ND_COMMA, ND_FUNCALL, ND_BLOCK)
4. 残余 (ND_PRE_INC, ND_POST_INC, ND_PRE_DEC, ND_POST_DEC, ND_NEG, ND_NOT, ND_FP_TO_INT 等)

各 helper は統一シグネチャ:
```c
static ir_val_t build_node_<kind>(ir_build_ctx_t *ctx, node_t *node);
```

**依存**: Phase A3 完了必須 (fp_kind ヘルパが効いて漏れ防止)

**Step**:
- **B1-1**: dispatch 骨格 + lvalue 系抽出
- **B1-2**: 算術系抽出
- **B1-3**: 制御系抽出
- **B1-4**: 残余抽出 + switch 削除

**完了条件**: `build_expr` < 100 行 (dispatch のみ)、各 helper < 200 行、820/820

**リスク**: 中。ローカル変数の隠れた依存 → commit ごとに probe diverged=0 で検証

**commit 数**: 4 / **時間感**: 6-10h

---

### B2: brace 初期化系の分解

**目的**: 大きめの brace init 関連関数群 (parse_struct_initializer 217行、parse_array_initializer、apply_toplevel_object_initializer 等) を整理。`brace_init_walker_t` 構造体で内部状態を集約

**対象ファイル**: `src/parser/decl.c`, `src/parser/parser.c`

**設計**: 内部状態を構造体に集約
```c
typedef struct {
  lvar_t *target;          // ローカル init の場合
  global_var_t *target_gv; // グローバル init の場合
  int cur_idx;
  int *cap;
  // ...
} brace_init_walker_t;
```

**Step**:
- **B2-1**: walker 構造体抽出
- **B2-2**: スカラ/集約分岐分離
- **B2-3**: 指示子と文字列リテラル分離

**完了条件**: 各関数 < 200 行

**リスク**: 中-高。designated initializer 系 e2e の集中。**designated init 系 probe 単独抽出スクリプト**を Step 開始前に用意

**commit 数**: 3 / **時間感**: 5-8h

---

### B3: `psx_decl_parse_declaration_after_type_ex` (364行) 分解

**目的**: 364 行を `parse_function_declarator` / `parse_array_declarator` / `parse_pointer_declarator` / `parse_init_declarator` に切り分け

**対象ファイル**: `src/parser/decl.c`

**依存**: A2 完了 (tag 設定ヘルパが効いて関数本体のロジック密度が下がる)

**Step**:
- **B3-1**: function-declarator 抽出
- **B3-2**: array/pointer 抽出
- **B3-3**: init-declarator 抽出

**完了条件**: 元関数 364 → < 150 行

**リスク**: 中。typedef + storage-class + alignas の組合せ

**commit 数**: 3 / **時間感**: 4-6h

---

### B4: `gen_global_vars` 分割

**目的**: struct ブランチ内の配列メンバ展開 (commit 71a1bbb で追加) を `emit_global_aggregate_init` に分離

**対象ファイル**: `src/arch/arm64_apple.c`

**Step**:
- **B4-1**: helper 抽出

**完了条件**: `gen_global_vars` < 150 行

**リスク**: 低

**commit 数**: 1 / **時間感**: 1-2h

---

**Phase B 小計**: 11 commits, 16-26h, regression リスク中 (A3/A2 が効いていれば抑制)

---

## Phase C: モジュール境界 (中〜高リスク)

### C1: `ast.h` 分割

**目的**: AST node 定義と シンボルテーブル (lvar_t/global_var_t) を分離

**対象ファイル**: 全 include 元 (parser/ir/arch 横断)

**設計**:
- `src/parser/ast.h` (AST node のみ、< 200 行)
- `src/parser/symtab.h` (lvar_t / global_var_t / global_vars 等、< 150 行)
- 旧 ast.h は両方を include する shim として残し、Phase 後半で撤去

**Step**:
- **C1-1**: symtab.h 切り出し + shim
- **C1-2**: parser 内 include 個別化
- **C1-3**: ir/arch include 個別化
- **C1-4**: shim 撤去

**完了条件**: `src/ir/` と `src/arch/` から ast.h への include が消える (シンボルだけ symtab.h で参照)

**リスク**: 中。ヘッダ循環依存検出に時間がかかる可能性

**commit 数**: 4 / **時間感**: 4-6h

---

### C2: parser → IR public I/F 抽出

**目的**: `src/ir/ir_builder.c` から `../parser/internal/semantic_ctx.h` の include を排除

**対象ファイル**:
- `src/ir/ir_builder.c`
- `src/parser/internal/semantic_ctx.h`
- 新設 `src/parser/parser_public.h`

**依存**: A1 完了必須 (`tag_member_info_t` が既にある)

**設計**: `parser_public.h` に IR が必要な公開 I/F のみ集約 (関数 prototype 情報、is_variadic、tag_member_info_t)。`internal/` は parser 内部のみ可

**Step**:
- **C2-1**: public ヘッダ新設
- **C2-2**: IR の include 切替
- **C2-3**: internal/ ヘッダの visibility 強化 (file scope の static 化など)

**完了条件**: `grep '../parser/internal' src/ir/*.c` = 0

**リスク**: 中。internal を直接参照していた他箇所の発見漏れ

**commit 数**: 3 / **時間感**: 3-5h

---

### C3: codegen と global_vars の結合緩和

**目的**: codegen が global_vars リストを直接舐めず、iterator/visitor 経由に

**対象ファイル**:
- `src/arch/arm64_apple.c`
- `src/codegen_backend.h`

**設計**:
```c
typedef void (*global_var_visitor_t)(global_var_t *gv, void *user);
void codegen_iter_globals(global_var_visitor_t fn, void *user);
```

**Step**:
- **C3-1**: iterator API 新設
- **C3-2**: arm64 移行

**完了条件**: arm64_apple.c から `extern global_var_t *global_vars` の直接参照が消える

**リスク**: 中

**commit 数**: 2 / **時間感**: 3-4h

---

### C4: IR Phase 8 (マルチターゲット) との整合確認

**目的**: 既存 `docs/ir_intermediate_representation/implementation_plan.md` Phase 8 の前提が C2/C3 で揃ったことを記録

**対象ファイル**: docs のみ (コード変更なしの想定)

**Step**:
- **C4-1**: Phase 8 計画と本計画の整合性記録、必要なら adapter 追加

**commit 数**: 1 / **時間感**: 1-2h

**リスク**: 低

---

**Phase C 小計**: 10 commits, 11-17h

---

## 各 Step 共通の Verification

```bash
cd /Users/iwao/Documents/GitHub/ag_c
make -s build/ag_c                                  # コンパイル通る
make -s test 2>&1 | tail -3                          # 820/820 維持
cd /tmp/probes && bash run_diff.sh > /tmp/diff.txt 2>&1
grep "diverged" /tmp/diff.txt                        # diverged=0 維持
```

Phase 完了時:
```bash
wc -l src/parser/*.c src/ir/*.c src/arch/*.c
grep -c "tag_kind = " src/parser/*.c
# metrics.md と該当 phase_*_walkthrough.md を更新
```

## 実装現実性評価

- **Phase A**: 純粋な抽出のみで動作変更ゼロ。820 e2e 通過は高確度で維持可能。**現実性: 高**
- **Phase B**: 関数分割は機械的だがローカル変数の生存範囲が罠。A3 が先行なら fp_kind 落ちは防げる。**現実性: 中-高**
- **Phase C**: ヘッダ分割は build system の `.d` 依存生成に依存。なければ clean build を毎 Step 強制。C2 は IR Phase 8 の伏線回収として正当な投資。**現実性: 中**

## Phase 完了 → 次 Phase 着手のフロー

各 Phase の最後の commit で:
1. `phase_<x>_walkthrough.md` を新規作成
2. 変更内容を表で一覧化 (commit hash, 行数 before/after, ヘルパ追加数)
3. `metrics.md` の該当指標を更新
4. ユーザーに Phase 完了報告 → 次 Phase 着手の確認

## 注意点

- **regression テストの flakiness**: 過去のセッションで `make test` の `probes` カテゴリビルドが flaky な挙動 (test_e2e.c 並列ビルドの race と推定)。各 Step で `make test` を 2 回連続実行
- **probe sweep の時間**: 246 件で 3-5 分かかる。Step ごと sweep は時間コスト高 → Phase 完了時の sweep を必須、Step 完了時は影響範囲の部分集合 (例: B1 中は IR builder 関連 probe) で済ませる
- **既存 IR Phase 8 との重複防止**: C4 で integration check、計画初期に IR Phase 8 implementation_plan を読み直す
