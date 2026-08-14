# JSON 出力仕様

生成・検証コマンドは、`.sfen.jsonl`（JSON Lines）形式で詳細記録を出力します。1 行 = 1 レコード = 1 局面です。

## WorkRecord スキーマ

`tsume_workbench.hpp:105-119` の `WorkRecord` 構造体が出力の元になっています。

```json
{
  "id": "unique record hash",
  "canonicalId": "座標変換不変の正規化ハッシュ",
  "sfen": "スパース SFEN（攻方持駒のみ）",
  "normalizedSfen": "座標正規化後 SFEN",
  "title": "",
  "author": "",
  "generated_at": "ISO 8601 タイムスタンプ",

  "verification": {
    "proof": "mate|nomate|repetition|unknown",
    "matePly": 9,
    "nodes": 123456,
    "complete": true,
    "unique": true,
    "alternativeMate": false,
    "shorterMate": false,
    "noMateBranch": false,
    "hendouCount": 0,
    "principal": ["3c5b", "5a4b", ...],
    "attacks": [
      { "move": "3c5b", "proof": "mate", "matePly": 9, "principal": [...] }
    ],
    "reasons": []
  },

  "scores": {
    "legalityScore": 100,
    "uniquenessScore": 100,
    "lengthScore": 60,
    "complexityScore": 55,
    "sacrificeScore": 40,
    "techniqueScore": 45,
    "economyScore": 70,
    "visualScore": 65,
    "originalityScore": 50,
    "aestheticScore": 58,
    "totalScore": 62,

    "x5Spread": 42,
    "x6Count": 8,
    "x7Weighted": 55,
    "x8Ratio": 62,
    "x10Difficulty": 30
  },

  "aestheticPass": true,
  "aestheticReasons": [],

  "parentId": null,
  "parentMatePly": null,
  "parentSimilarity": null,
  "parentDiff": null,
  "extensionReason": null
}
```

## フィールド解説

### 局面情報

- **`id`**: レコード固有のハッシュ
- **`canonicalId`**: **座標変換不変** の正規化ハッシュ。左右反転・平行移動しても同じ値になるので、**シンメトリ重複検出**に使える
- **`sfen`**: 生成時のスパース SFEN（39/40 枚制補完前）
- **`normalizedSfen`**: 座標正規化後 SFEN（キャノニカル形）

### 検証結果（`verification`）

- **`proof`**: `mate` / `nomate` / `repetition` / `unknown`
- **`matePly`**: 詰み手数
- **`nodes`**: `ExhaustiveVerifier` が展開したノード数
- **`complete`**: 全探索が上限内で完結したか
- **`unique`**: 攻方初手唯一（余詰なし）
- **`alternativeMate`** / **`shorterMate`**: 別解・短手数解の有無
- **`hendouCount`**: 変同数（同一手数の別解総数、累計）
- **`principal`**: 最短の作意手順（USI 表記）
- **`attacks`**: 全ルート攻手の分類
- **`reasons`**: エラー・警告メッセージ

### 美的スコア（`scores`）

11 次元スコアの詳細は [検証と美的評価](verification.md) 参照。

### X 変数（廣瀬 1998）

廣瀬 [1998] の詰将棋自動評価の重回帰式で使われた変数を、JSON に個別出力しています。

| 変数 | 意味 | 符号 |
|---|---|---|
| X3 | 攻方の駒取り数 | 負（少ないほど良い） |
| X4 | 詰め上がり玉の開放度 | 正 |
| X5 | バウンディングボックス広がり | 0-100 |
| X6 | 盤上駒数 | — |
| X7 | 駒種加重平均 | 0-100 |
| X8 | 駒種数 / 駒総数 | 0-100 |
| X9 | 攻方駒稼働率 | 0-100 |
| X10 | 探索難度（ノード数を 0-100 に正規化） | — |
| X12 | 玉端距離 | 正 |

これらは `x5Spread`, `x6Count`, `x7Weighted`, `x8Ratio`, `x10Difficulty` として保存されます（X3・X4・X9・X12 は `TechniqueSummary` 側に含まれます）。

### 派生情報（`tsume_extend` 由来）

`user tsume_extend` で親局面から派生した記録には、以下が付与されます。

- **`parentId`**: 親レコードの ID
- **`parentMatePly`**: 親の詰み手数
- **`parentSimilarity`**: 親との類似度（0.0〜1.0）
- **`parentDiff`**: 差分の種類（駒移動 / 除去 / 種変更 / 成トグル / …）
- **`extensionReason`**: 拡張理由（王手追加 / 逃げ道追加 / …）

### `aestheticPass` / `aestheticReasons`

- `aestheticPass == true` なら SFEN 側にも問題が保存される
- `false` の場合、`aestheticReasons` に落ちた理由が入る（例: `"chasing mate"`, `"too few pieces"`, `"no technique"`）

## 使い方例

### JSONL を統計解析

```python
import json
records = [json.loads(line) for line in open("problems.sfen.jsonl")]
mate_only = [r for r in records if r["verification"]["proof"] == "mate"]
aesthetic = [r for r in mate_only if r["aestheticPass"]]
print(f"{len(aesthetic)} / {len(records)} passed aesthetic gate")
```

### canonicalId で重複除去

```python
seen = set()
unique = []
for r in records:
    cid = r["canonicalId"]
    if cid in seen: continue
    seen.add(cid)
    unique.append(r)
```

### X 変数を機械学習の特徴量として利用

```python
X = [[r["scores"]["x5Spread"], r["scores"]["x6Count"], r["scores"]["x7Weighted"],
      r["scores"]["x8Ratio"], r["scores"]["x10Difficulty"]]
     for r in aesthetic]
y = [r["scores"]["aestheticScore"] for r in aesthetic]
```

## JSON 出力の履歴（コミット履歴より）

- `ec3dcc0b` — JSON プレフィックス修正 + `batch_eval` で全検証なしに X 変数を収集可能に
- `36c4b69a` — X5〜X10 を JSON 出力に追加
- `3d258d61` — 変同（`hendouCount`）検出を追加
