// NNUE評価関数の入力特徴量HalfKPE9の定義

#include "../../../config.h"

#if defined(EVAL_NNUE)

#include "half_kpe9.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// BonaPieceからSquareを取得する
// ・持ち駒の場合はkSquareNoneを返す。
inline Square GetSquareFromBonaPiece(BonaPiece p) {
  if (p < fe_hand_end) {
    return kSquareNone;
  }
  else {
    return static_cast<Square>((p - fe_hand_end) % SQ_NB);
  }
}

// 利き数の取得
inline int GetEffectCount(const Position& pos, Square sq_p, Color perspective_org, Color perspective, bool prev_effect) {
  if (sq_p == kSquareNone) {
    return 0;
  }
  else {
    if (perspective_org == WHITE) {
      sq_p = Square::rotate180(sq_p);
    }

    if (prev_effect) {
      return std::min(pos.previous_num_controls(perspective, sq_p), 2);
    }
    else {
      return std::min(pos.num_controls(perspective, sq_p), 2);
    }
  }
}

// BonaPieceのdirty判定
inline bool IsDirty(Color perspective, const Eval::DirtyPiece& dp, BonaPiece p) {
  for (int i = 0; i < dp.dirty_num; ++i) {
    if (dp.pieceNo[i] >= PIECE_NUMBER_KING) continue;

    const auto new_p = static_cast<BonaPiece>(dp.changed_piece[i].new_piece.from[perspective]);
    if (p == new_p) {
      return true;
    }
  }
  return false;
}

// 玉の位置とBonaPieceと利き数から特徴量のインデックスを求める
template <Side AssociatedKing>
inline IndexType HalfKPE9<AssociatedKing>::MakeIndex(Square sq_k, BonaPiece p, int effect1, int effect2) {
  return (static_cast<IndexType>(fe_end) * static_cast<IndexType>(sq_k) + p)
       + (static_cast<IndexType>(fe_end) * static_cast<IndexType>(SQ_NB) * (effect1 * 3 + effect2));
}

// 特徴量のうち、値が1であるインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKPE9<AssociatedKing>::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  PsqList* psq_list = pos.GetPsqList();

  Square sq_target_k = pos.king_square(perspective);
  if (perspective == WHITE) {
    sq_target_k = Square::rotate180(sq_target_k);
  }

  for (const PsqPair* psq_pair = psq_list->begin(); psq_pair != psq_list->end(); ++psq_pair) {
    PsqIndex psq_index = (perspective == kBlack) ? psq_pair->black() : psq_pair->white();
    Square sq_p = psq_index.square();
    BonaPiece bp = (BonaPiece)GetNnuePsqIndex(psq_index);
    active->push_back(MakeIndex(sq_target_k, bp
        , GetEffectCount(pos, sq_p, perspective, perspective, false)
        , GetEffectCount(pos, sq_p, perspective, ~perspective, false)
      ));
  }
}

// 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKPE9<AssociatedKing>::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {

  PsqList* psq_list = pos.GetPsqList();

  Square sq_target_k = pos.king_square(perspective);
  if (perspective == WHITE) {
    sq_target_k = Square::rotate180(sq_target_k);
  }

  const auto& dp = pos.state()->dirtyPiece;

  for (int i = 0; i < dp.dirty_num; ++i) {
    if (dp.pieceNo[i] >= PIECE_NUMBER_KING) continue;

    const auto old_p = static_cast<BonaPiece>(dp.changed_piece[i].old_piece.from[perspective]);
    Square old_sq_p = GetSquareFromBonaPiece(old_p);
    removed->push_back(MakeIndex(sq_target_k, old_p
        , GetEffectCount(pos, old_sq_p, perspective, perspective, true)
        , GetEffectCount(pos, old_sq_p, perspective, ~perspective, true)
      ));

    const auto new_p = static_cast<BonaPiece>(dp.changed_piece[i].new_piece.from[perspective]);
    Square new_sq_p = GetSquareFromBonaPiece(new_p);
    added->push_back(MakeIndex(sq_target_k, new_p
        , GetEffectCount(pos, new_sq_p, perspective, perspective, false)
        , GetEffectCount(pos, new_sq_p, perspective, ~perspective, false)
      ));
  }

  for (const PsqPair* psq_pair = psq_list->begin(); psq_pair != psq_list->end(); ++psq_pair) {
    PsqIndex psq_index = (perspective == kBlack) ? psq_pair->black() : psq_pair->white();
    BonaPiece p = (BonaPiece)GetNnuePsqIndex(psq_index);

    if (IsDirty(perspective, dp, p)) {
      continue;
    }

    Square sq_p = GetSquareFromBonaPiece(p);

    int effectCount_prev_1 = GetEffectCount(pos, sq_p, perspective, perspective, true);
    int effectCount_prev_2 = GetEffectCount(pos, sq_p, perspective, ~perspective, true);
    int effectCount_now_1 = GetEffectCount(pos, sq_p, perspective, perspective, false);
    int effectCount_now_2 = GetEffectCount(pos, sq_p, perspective, ~perspective, false);

    if (   effectCount_prev_1 != effectCount_now_1
        || effectCount_prev_2 != effectCount_now_2) {
      removed->push_back(MakeIndex(sq_target_k, p, effectCount_prev_1, effectCount_prev_2));
      added->push_back(MakeIndex(sq_target_k, p, effectCount_now_1, effectCount_now_2));
    }
  }
}

template class HalfKPE9<Side::kFriend>;
template class HalfKPE9<Side::kEnemy>;

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
