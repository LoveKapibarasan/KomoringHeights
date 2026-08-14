# USI コマンド

このプロジェクトは全て **`user <subcommand>`** の形で USI に拡張コマンドを追加しています。ディスパッチは `user-search.cpp` の `user_test()` で行われます。

## コマンド一覧

| コマンド | 目的 |
|---|---|
| [`user tsume_solve`](#user-tsume_solve) | 現局面が詰むか判定（df-pn） |
| [`user tsume_generate`](#user-tsume_generate) | 詰将棋を生成 |
| [`user tsume_batch_generate`](#user-tsume_batch_generate) | 複数手数を一括生成 |
| [`user tsume_verify`](#user-tsume_verify) | 全探索で完全性検証 |
| [`user tsume_extend`](#user-tsume_extend) | 現局面から近傍問題を派生生成 |
| [`user tsume_save`](#user-tsume_save--user-tsume_load) | 局面を検証して JSONL に保存 |
| [`user tsume_load`](#user-tsume_save--user-tsume_load) | JSONL を再検証 |

事前に `position sfen <SFEN>` などで局面をセットしてから呼び出します。

---

## `user tsume_solve`

現局面が詰むかを df-pn で判定します。ルールチェック（受方玉の存在、攻方手番など）も含みます。

**構文**: `user tsume_solve`

**出力例**:
```
info string [tsume_solve] 9手詰め
checkmate 3c5b 5a4b 3b3a
info string [tsume_solve] 詰まない
checkmate nomate
info string [tsume_solve] 千日手
info string [tsume_solve] 探索打切
checkmate timeout
```

---

## `user tsume_generate`

条件を満たす詰将棋を自動生成します。

**構文**:
```
user tsume_generate <target_moves> [count] [output_file]
```

| 引数 | デフォルト | 制約 |
|---|---|---|
| `target_moves` | 必須 | 7 以上の**奇数**（7, 9, 11, …） |
| `count` | 1 | 生成する問題数 |
| `output_file` | `tsume_output.sfen` | 出力パス（`.jsonl` は自動付与） |

**内部フロー**:
1. `output_file` があれば既存問題を読み込み（重複回避）
2. 70% 確率: **逆算法**（`ReverseGenerateNPlySfen`）
3. 30% 確率（9手専用）: **構造化シード**（`GenerateStructuredSeed9ply`）
4. フォールバック: ランダム候補（`GenerateCandidateSfen`）
5. df-pn 検証（手数ごとに時間制限）
6. `ExhaustiveVerifier` で完全性検証
7. 美的スコア計算
8. `.sfen` に SFEN 1 行、`.sfen.jsonl` に完全記録を追記

詳細は [生成アルゴリズム](flow.md) を参照。

---

## `user tsume_batch_generate`

複数の手数を一括生成します。

**構文**:
```
user tsume_batch_generate <start> <end> <step> [count_each] [output_file]
```

**例**:
```
user tsume_batch_generate 5 31 2 2 batch.sfen
# → 5, 7, 9, ..., 31 手詰めを各 2 題ずつ batch.sfen に出力
```

---

## `user tsume_verify`

現局面に対して全探索で完全性検証を実行します。

**構文**:
```
user tsume_verify [max_ply] [max_nodes] [known_move1 known_move2 ...]
```

- `max_ply`: 全探索の最大手数（既定値あり）
- `max_nodes`: 全探索のノード数上限
- `known_move*`: 既知の作意手順（USI 表記）を与えると検証が高速化

**出力**: JSON 形式で以下を返します（`.jsonl` レコードと同じスキーマ）:

- 詰み証明・反証・不明の判定
- 全ルート攻手の分類（作意 / 別解 / 短手数 / 詰まず）
- 変同数 (`hendouCount`): 同じ手数の別解の数
- 美的スコア 11 次元 + 技巧サマリ
- X 変数 X3〜X12

詳細は [JSON 出力仕様](json-format.md) を参照。

---

## `user tsume_extend`

現局面から**局所変形**によって近傍の問題を生成します。詰将棋作家の「原案から派生を試す」プロセスを機械化するイメージです。

**構文**:
```
user tsume_extend <count> <max_ply> <max_nodes> <jsonl_file>
```

**変形操作**（6 種類）:
1. 駒の移動
2. 駒の除去
3. 駒種の変更
4. 成/不成トグル
5. 受方玉の移動
6. 攻方駒の追加

**フィルタ**: 完全性 + 唯一解 + 美的合格のみを保存。親子関係（`parentId`, `parentSimilarity`, `extensionReason`）が JSON に記録されます。

---

## `user tsume_save` / `user tsume_load`

局面（または既存 JSONL）を全探索検証してファイルに保存・再検証します。ラウンドトリップテストや大規模データセット構築に使います。

**構文**:
```
user tsume_save <max_ply> <max_nodes> <jsonl_file> [known_move1 known_move2 ...]
user tsume_load <jsonl_file> [max_ply] [max_nodes]
```

`tsume_save` は現局面を検証して JSONL に追記、`tsume_load` は既存 JSONL の全レコードを再検証します。

---

## 生成時のパラメータ調整

生成の内部パラメータ（時間制限・シード比率など）は主に `tsume_generator.hpp` と `user-search.cpp` の定数で決まっています。代表的な値:

- `max_retries = 200`: 逆算 1 コールあたりの試行回数
- `cand_limit_per_step = 6`: 逆算 1 ステップあたりの候補上限
- `ComputeTimeLimitMs(ply)`: 手数に応じた df-pn 時間制限（500ms → 4s → 12s）
