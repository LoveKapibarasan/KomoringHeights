# ビルドと開発

## 対応環境

| 項目 | 内容 |
|---|---|
| 言語 | C++17（`std::optional`, `std::filesystem` 前提） |
| コンパイラ | GCC 11+ または Clang 14+ |
| 対応 OS | Windows（MSYS2/MinGW）、Linux、macOS |
| CPU 拡張 | AVX2 / AVX512 / SSE42 / ZEN1/2/3 など |

## ビルド

### Windows（MSYS2/MinGW, 推奨）

```bash
cd source
make normal TARGET_CPU=AVX2 COMPILER=g++ YANEURAOU_EDITION=USER_ENGINE
```

生成物: `source/KomoringHeights-by-gcc.exe`

### Linux / macOS

```bash
cd source
make normal TARGET_CPU=AVX2 COMPILER=clang++ YANEURAOU_EDITION=USER_ENGINE
```

### 主要ビルドオプション

- `TARGET_CPU`: `AVX2` / `AVX512` / `SSE42` / `ZEN1` / `ZEN2` / `ZEN3` / `NO_SSE`
- `COMPILER`: `g++` / `clang++` / `x86_64-w64-mingw32-g++`
- `YANEURAOU_EDITION`: 常に `USER_ENGINE`（このプロジェクトのモード）

## 実行

```bash
./KomoringHeights-by-gcc.exe
```

USI プロトコルで待ち受けます。事前に GUI（将棋所・ShogiGUI 等）から起動してもよいし、直接コマンド入力してもよい。

```
usi
isready
position sfen <SFEN>
user tsume_solve
```

## 回帰テスト

Windows 側は PowerShell テストスクリプトで一括確認できます。

```powershell
.\script\test_tsume_mode.ps1 -Engine .\source\KomoringHeights-by-gcc.exe
```

テスト内容:
- USI 通信の正常動作
- 完全駒集合復元（39/40 枚制）
- `tsume_save` / `tsume_load` のラウンドトリップ
- 座標変換不変な `canonicalId`
- 無効局面の安全な拒否
- 既存の求解モード（`go mate`, `user tsume_solve`）

## 拡張のヒント

### 生成戦略を追加する

`user-search.cpp` の生成ディスパッチ（`user_test()` 内、`tsume_generate` の分岐）で新しい生成関数をフォールバックとして追加できます。**確率で分岐**すること（[禁止禁止の原則](principles.md)）。

### 新しい美的スコアを追加する

1. `tsume_workbench.hpp` の `AestheticScores` 構造体に新フィールドを追加
2. `TechniqueSummary` 側で必要な統計を集計
3. スコア算出ロジックを追加
4. `WorkRecord` の JSON 出力に新フィールドを追記
5. `docs/tsume-aesthetic-policy.md` のスコア原則に方針を追記

### 新しい USI コマンドを追加する

`user-search.cpp` の `user_test()` のトップレベル分岐に `else if (subcommand == "your_command")` を追加します。

## デバッグ

### 生成ログの解読

生成中は `info string [gen]` プレフィックスで進捗が出力されます。以前は `gen_log_*.txt` として保存していましたが、実験ログが散乱するので現在は `.gitignore` で除外しています。

### JSONL の解析

Python の `json.loads` で 1 行ずつパースできます。フィールド仕様は [JSON 出力仕様](json-format.md) を参照。

## コード規約

- **禁止禁止の原則**: 生成・探索コードでは「skip/continue で候補を完全排除」を避け、確率を下げる方向で実装（詳細は [設計原則](principles.md)）
- **39 枚制**: スパース SFEN を最終解として保存しない
- **完全性 = 全探索**: ヒューリスティックで作意成立を判断しない

コミット時の pre-commit hook（`.pre-commit-config.yaml`）で lint がかかります。
