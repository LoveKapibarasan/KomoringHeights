/**
 * @file tsume_validator.hpp
 * @brief 詰将棋ルールに基づく局面検証
 *
 * 詰将棋ルール（https://www.shogitown.com/tume/guide/guide.html）:
 *   1. 攻め方は毎手王手をかけなければならない
 *   2. 受け方は最善手で応じなければならない
 *   3. 打ち歩詰め禁止（合法手生成で除外済み）
 *   4. 連続王手の千日手は攻め方の負け（repetition 検出で処理済み）
 *   5. 受け方の持ち駒はすべて使える
 */
#ifndef KOMORI_TSUME_VALIDATOR_HPP_
#define KOMORI_TSUME_VALIDATOR_HPP_

#include <string>
#include <array>

#include "../../position.h"
#include "../../types.h"

namespace komori {

/**
 * @brief 詰将棋として有効な局面かを検証する
 * @param pos 検証対象の局面
 * @return 有効なら空文字列、無効ならエラー理由の文字列
 *
 * 検証項目:
 *   - 受け方（攻め方の相手）の玉が盤上にある
 *   - 攻め方の手番である（OR ノードとして探索できる）
 */
inline std::string ValidateTsumePosition(const Position& pos) {
  if (pos.side_to_move() != BLACK) {
    return "攻方は先手番(b)でなければなりません";
  }
  const std::array<PieceType, 7> types{{PAWN, LANCE, KNIGHT, SILVER, GOLD, BISHOP, ROOK}};
  const std::array<int, 7> maxima{{18, 4, 4, 4, 4, 2, 2}};
  std::array<int, 7> counts{};
  for (int s = 0; s < SQ_NB; ++s) {
    const Piece pc = pos.piece_on(static_cast<Square>(s));
    if (pc == NO_PIECE || raw_type_of(pc) == KING) continue;
    for (std::size_t i = 0; i < types.size(); ++i) if (raw_type_of(pc) == types[i]) ++counts[i];
  }
  for (std::size_t i = 0; i < types.size(); ++i) {
    counts[i] += hand_count(pos.hand_of(BLACK), types[i]) + hand_count(pos.hand_of(WHITE), types[i]);
    if (counts[i] > maxima[i]) return "完全駒集合を超える駒があります";
  }
  const Color us   = pos.side_to_move();  // 攻め方
  const Color them = ~us;                  // 受け方

  // 受け方の玉が盤上になければ詰将棋にならない
  if (pos.king_square(them) == SQ_NB) {
    return "受け方の玉が盤上にありません";
  }

  // 出題局面で攻め方に王手がかかっていてはならない
  // （玉方手番の局面は「受け方から始まる詰将棋」であり通常形式ではない）
  // RootIsAndNodeIfChecked オプションで対処可能だが、ここでは通知のみ
  if (pos.in_check()) {
    // 注意: 王手がかかった状態で開始する局面（玉方手番）は
    // RootIsAndNodeIfChecked=true で対処できる
    return "出題局面で受け方に王手がかかっています（RootIsAndNodeIfChecked を確認してください）";
  }

  return "";
}

}  // namespace komori

#endif  // KOMORI_TSUME_VALIDATOR_HPP_
