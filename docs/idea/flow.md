# 生成アルゴリズム

詰将棋の自動生成は、**逆算法** を主軸に、**構造化シード** と **ランダム候補** を補助として使う3段構えです。

## 全体フロー

```
user tsume_generate 9 5 problems.sfen
        │
        ▼
既存 SFEN を読み込み（重複回避）
        │
        ├── 70% ─▶ ReverseGenerateNPlySfen（逆算法）
        ├── 30% ─▶ GenerateStructuredSeed9ply（構造化シード, 9手のみ）
        └── fallback ─▶ GenerateCandidateSfen（ランダム）
        │
        ▼
df-pn で作意検出（時間制限付き）
        │
        ▼
ExhaustiveVerifier で完全性確認
        │
        ▼
CompleteDefenderReserve で 39/40 枚制に補完
        │
        ▼
AestheticScores + TechniqueSummary を計算
        │
        ▼
SFEN + JSONL に追記保存
```

割合 70/30/fallback は `user-search.cpp:2517-2570` あたりで決定されます。

---

## 1. 逆算法（Retrograde）

**方向**: 詰め上がり局面 → 逆に指し戻して → 開始局面

### アルゴリズム（`ReverseGenerateNPlySfen`, `user-search.cpp:2324-2420`）

1. **シード**: `GenerateMatedSfen` で「詰み上がり」局面を作る（mate_ply=0）
2. **状態スタック**: 各ステップに `(候補リスト, 次インデックス, 摂動フラグ)` を積む
3. **前進**:
   - **攻方ステップ**（偶数手前）: `EnumerateAttackerReverses` + `VerifyMateExactly`（最終手のみ唯一性厳密検証）
   - **受方ステップ**（奇数手前）: `EnumerateDefenderReverses` + `DefenderPositionOK`（軽量検証）
4. **摂動**: ステップ N で候補が尽きたら、ステップ N-1 に戻って**駒の追加/削除**でわずかに変形し再列挙
5. **バックトラック**: 摂動が失敗ならスタックを pop してさらに戻る
6. **終了**: 目標手数 `target_moves` に到達したら停止

### 主要定数

| 定数 | 値 | 意味 |
|---|---|---|
| `max_retries` | 200 | 1 コールあたりの試行回数 |
| `cand_limit_per_step` | 6 | 1 ステップの候補上限 |
| `max_verifies` | 250 | 1 試行の軽量検証コール上限 |

### 逆算の利点

- **収束保証**: 目標手数から始めるので、必ず「N 手詰めになる」形が得られる
- **駒余り制御**: 逆算過程で使う駒は最小限で済ませられる
- **芸術性**: 詰め上がりを先に決めるため、意味のない持駒が減る

---

## 2. 構造化シード（Structured Seed）

**目的**: 9手詰めで高い成功率を得るため、**手作りのテンプレート**からシードを生成します。

### 実装 (`GenerateStructuredSeed9ply`, `tsume_generator.hpp:325-398`)

- **80% Dense**（meta 0-3）: 歩壁 + 後衛型
  - 受方玉: 1 段目、筋 1〜7
  - 黒歩 9 枚（1 段目全域）で上への逃げを塞ぐ
  - 黒駒（銀・金・香・桂）を 7〜8 段目に配置
  - 攻方: 3 段目に桂、4 段目に銀
  - 攻方持駒（meta 別）: `{BG, BGG, RG, RBG}`

- **20% Sparse**（meta 4）: 従来型スパース
  - 玉を 1〜2 段目中央付近
  - 近傍受方 + 下方の攻方桂

### なぜ密配置か

スパース局面は逃げ道・中合の候補が多すぎて df-pn の分岐が爆発しがち。密配置は**受方持駒を 14〜18 枚**に減らすことで、df-pn が詰みを見つけやすくなります（コメント `tsume_generator.hpp:251-255`）。

---

## 3. ランダム候補（Random Candidate）

**位置付け**: 逆算・構造化シードの両方が失敗したときのフォールバック。

### 玉配置バイアス (`GenerateCandidateSfen`, `tsume_generator.hpp:129-317`)

- 9 手詰め: 60% を上位 4 段（段 0〜3）にバイアス — 逃げスペース確保
- 9 手未満: 75% を上位 3 段（端付近）にバイアス — 端詰め狙い

### 駒配置ルール

| 駒種 | 制約 |
|---|---|
| 大駒（飛・角） | 攻方合計 **1 枚まで**（複数だと詰みが不定になる） |
| 金 | 合計 **2 枚まで**（3 枚以上は変化が多すぎる） |
| 歩 | 極力持駒（二歩を避けるため） |
| 攻方駒数 | 1〜6 枚（ply により上限が変化） |
| 位置 | 65% で玉近傍（散らばりは ply 依存） |

### 受方配置

- 玉近傍に 0〜3 枚を追加（合理的な受け候補を用意）
- 二歩・王手放置は排除

---

## 4. 逆算連鎖戦略（(N-2) → N 手）

**入力**: (target_moves - 2) 手詰め局面 → **target_moves 手詰め**

### パイプライン (`user-search.cpp:2656-2782`)

1. **39 枚直接変換**: 既存スパース SFEN がそのまま目標手数になるなら、検証して保存
2. **逆算 1 ステップ**: `RetrogradStep`（玉手の追加のみ、駒の削減はしない）で 2 手増やす
3. **BFS キュー**: 中間段階の局面を溜めて並列探索
4. **最終拡張**: `RetrogradExtend`（駒除去・再配置・手主導反転を含む総合逆算）
5. **39 枚確認**: 完全駒セットに戻しても解が保たれるか確認

### 予算管理

- `RetrogradStep`: 30 秒 / コール
- `RetrogradExtend`: 120 秒（BFS 供給時は 45 秒）
- 局面あたり df-pn 時間: `ComputeTimeLimitMs(ply)` — 500ms → 4s → 12s とスケール

---

## 生成が失敗する主なパターン

| 症状 | 原因 | 対策 |
|---|---|---|
| 何度やっても目標手数にならない | 玉配置が悪く逃げ道が多すぎる | 構造化シード比率を上げる |
| 変同ばかり出る | 攻方の初手候補が多すぎる | 大駒制限（1 枚まで）を守る |
| 追い詰めばかり | 攻方の駒種が偏っている | 桂・銀・金を混ぜる（ランダム候補側で対応済） |
| 時間切れが多い | 局面規模に対して時間制限が短い | `ComputeTimeLimitMs` を調整 |

## 実装ファイル参照

- `tsume_generator.hpp`: `ReverseGenerateNPlySfen`, `GenerateStructuredSeed9ply`, `GenerateCandidateSfen`, `EnumerateAttackerReverses`, `EnumerateDefenderReverses`
- `user-search.cpp`: 生成ディスパッチ (`user_test()` 内)、`RetrogradStep`, `RetrogradExtend`
