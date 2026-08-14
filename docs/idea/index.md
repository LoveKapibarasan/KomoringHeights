# KomoringHeights Tsume Wiki

このリポジトリは、詰将棋エンジン **KomoringHeights**（df-pn+ ベース、やねうら王フレームワーク上）に、**詰将棋の自動生成・検証・美しさ評価**を行う独自コードを追加したフォークです。

将棋エンジンとしての単なる「詰み探索」ではなく、以下を目的にしています。

- **自動生成**: 7手・9手・…・31手詰めの問題を機械的に作る
- **完全性検証**: AND/OR 全探索で「作意唯一」「余詰なし」「駒余りなし」を確認
- **芸術性評価**: 廣瀬（1998）の回帰式に基づく美的スコアと、追い詰めペナルティ

---

## 主要ドキュメント

| 章 | 内容 |
|---|---|
| [概要](overview.md) | このプロジェクトが何であり、何を目指しているか |
| [アーキテクチャ](architecture.md) | ディレクトリ構成と主要ファイル |
| [設計原則](principles.md) | 禁止禁止の原則・39枚制・確率的探索 |
| [USI コマンド](mode.md) | `user tsume_solve` / `tsume_generate` / … の使い方 |
| [生成アルゴリズム](flow.md) | 逆算法・構造化シード・ランダム候補 |
| [検証と美的評価](verification.md) | ExhaustiveVerifier と AestheticScores |
| [JSON 出力仕様](json-format.md) | WorkRecord と X 変数 |
| [ビルドと開発](how-to-develop.md) | Makefile と実行方法・拡張のヒント |

---

## クイックスタート

```bash
# ビルド（Windows / MSYS2）
cd source
make normal TARGET_CPU=AVX2 COMPILER=g++ YANEURAOU_EDITION=USER_ENGINE

# USI コマンドで詰将棋を解く
./KomoringHeights-by-gcc.exe
> position sfen <SFEN>
> user tsume_solve

# 9手詰めを 5 題生成
> user tsume_generate 9 5 problems.sfen
```

出力は `problems.sfen`（SFEN 一行問題集）と `problems.sfen.jsonl`（検証結果・美的スコア込みの詳細記録）です。

---

## 関連資料

- [逆算法論文（PDF）](reverse_method_paper.pdf)
- [詰将棋モード（正規リファレンス）](tsume-mode.md) — 上位仕様
- [詰将棋生成の審美方針](tsume-aesthetic-policy.md) — 美的評価の正規ポリシー
- [USI 拡張コマンド一覧](usi-extension-commands.txt) — やねうら王本体の USI 拡張
- 上位 README: `README.md`, `README.en.md`
