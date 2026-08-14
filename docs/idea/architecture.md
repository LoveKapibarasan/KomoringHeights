# アーキテクチャ

## リポジトリ構成

```
KomoringHeights/
├── source/                     # C++ ソースツリー（やねうら王ベース）
│   ├── engine/
│   │   └── user-engine/        # 本フォークの独自コード（詰将棋生成・検証）
│   ├── Makefile                # ビルド設定（USER_ENGINE がターゲット）
│   ├── main.cpp / position.cpp / movegen.cpp / …  # やねうら王本体
│   └── KomoringHeights-by-gcc.exe   # ビルド生成物
├── docs/                       # 上位ドキュメント（拡張仕様・美的方針）
│   ├── tsume-mode.md
│   ├── tsume-aesthetic-policy.md
│   ├── USI拡張コマンド.txt
│   └── idea/                   # 本 Wiki
├── script/                     # ヘルパスクリプト（PowerShell）
├── tools/                      # 補助ツール（KIF ↔ SFEN 変換など）
├── third-party/                # サブモジュール
├── CLAUDE.md                   # Claude Code 用のプロジェクト規約
└── README.md / README.en.md
```

## 主要ファイル（`source/engine/user-engine/`）

| ファイル | 役割 | サイズ感 |
|---|---|---|
| `komoring_heights.hpp` / `.cpp` | df-pn+ 詰み探索本体 | ~2 kLOC |
| `tsume_generator.hpp` | 詰将棋の**生成**（逆算・構造化・ランダム） | ~815 lines |
| `tsume_workbench.hpp` | **完全性検証**（ExhaustiveVerifier）と JSON レコード | ~1.2 kLOC |
| `tsume_validator.hpp` | ルール適合性検証（王の位置、手番など） | 小 |
| `user-search.cpp` | USI コマンドディスパッチ・生成パイプライン | ~3.5 kLOC |

上記以外の `.hpp`（`node.hpp`, `transposition_table.hpp`, `local_expansion.hpp` など）は KomoringHeights オリジナル本体の df-pn 実装です。

## レイヤ関係

```
┌──────────────────────────────────────┐
│  USI コマンド層 (user-search.cpp)      │  user_test() → dispatch
├──────────────────────────────────────┤
│  生成層 (tsume_generator.hpp)          │  ReverseGenerateNPlySfen ほか
├──────────────────────────────────────┤
│  検証層 (tsume_workbench.hpp)          │  ExhaustiveVerifier
├──────────────────────────────────────┤
│  df-pn 探索 (komoring_heights.hpp)     │  高速な詰み判定エンジン
├──────────────────────────────────────┤
│  やねうら王本体 (position, movegen…)     │  局面表現・合法手生成
└──────────────────────────────────────┘
```

## 依存関係

- **C++17** 必須（`std::optional`, `std::filesystem` を利用）
- **やねうら王フレームワーク**: ビットボード・合法手生成・USI プロトコル
- **KomoringHeights**: df-pn+ の探索本体
- **third-party/**: サブモジュール（詳細は `.gitmodules` 参照）
- **tsshogi**（外部ツール `tools/` 側）: KIF → SFEN バッチ変換に使用

## エントリポイント

- `main.cpp`: やねうら王標準の USI ループ
- `user-search.cpp` の `user_test()`: `user <subcommand>` を解析し、各モードにディスパッチ

例:

```
user tsume_solve                       → 詰み判定のみ
user tsume_generate 9 5 out.sfen       → 9手詰めを 5 題生成
user tsume_verify 31 5000000           → 現局面を全探索検証
```

コマンド一覧は [USI コマンド](mode.md) を参照。
