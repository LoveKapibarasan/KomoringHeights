# 詰将棋モード

既存の `go mate` / `user tsume_solve` は高速な df-pn 求解モードとして維持されています。作品としての完全性を検査するときは、全幅 AND/OR 探索を行う次のコマンドを使います。

```text
position sfen <SFEN>
user tsume_verify <上限手数> <上限ノード数> [既知のUSI手順...]
```

攻方は先手で、攻方ノードでは合法な王手だけ、玉方ノードでは無駄合を除く全合法回避手を探索します。上限ノード数に達した検査は `complete:false` となり、正式な完成作品にはなりません。SFENで省略された玉方の残り持駒は、単玉39枚または双玉40枚の完全駒集合との差分から復元され、完全なSFENとして探索されます。

結果は `info string tsume_json {...}` として出力されます。詰み手数、USI手順、人間向け手順、同手数変化、余詰、早詰、千日手、駒余り、不要駒、探索ノード数、技巧、11種の評価スコアと判定理由を含みます。

## 保存と再検証

```text
user tsume_save <上限手数> <上限ノード数> <JSONLファイル> [既知のUSI手順...]
user tsume_load <JSONLファイル> [上限手数] [上限ノード数]
```

`tsume_load` は保存SFENを再び完全探索するため、保存時の結果を再現検査できます。SFEN単体はJSONの `sfen` / `normalizedSfen` から再出力できます。

## 生成と手数延長

```text
user tsume_generate <7以上の奇数手数> [件数] [SFENファイル]
user tsume_batch_generate <開始手数> <終了手数> <step> [各件数] [SFENファイル]
user tsume_extend <件数> <上限手数> <上限ノード数> <JSONLファイル>
```

生成器は候補を df-pn で絞った後、全幅検証、一意性、駒余り、不要駒、正規化後の再検証を通過した作品だけを保存します。SFENファイルと同時に `<ファイル名>.jsonl` が保存されます。`tsume_extend` は現在局面に駒の追加・削除・移動・駒種変更・成不成変更・玉移動を加え、複数の完全作候補を保存します。各レコードには親ID、差分、変更前後の手数、類似度と延長理由が含まれます。

正規化では上移動、右移動、左右反転の各候補を再び完全探索し、合法手・詰み手数・変化構造が同型の場合だけ変換します。同型作品には正規化SFEN由来の共通 `canonicalId` が付与されます。

## 回帰テスト

Windowsビルド後は次のテストで、USI通信、完全駒集合復元、保存再読込、平行移動のcanonical ID、無効局面の安全な拒否、既存解答モードを一括確認できます。

```powershell
.\script\test_tsume_mode.ps1 -Engine .\source\KomoringHeights-by-gcc.exe
```
