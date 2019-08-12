/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "psq.h"

#include "position.h"

ArrayMap<PsqIndex, Color, PieceType> PsqIndex::hand_;
ArrayMap<PsqIndex, Square, Piece> PsqIndex::psq_;
Array<Piece, 2110> PsqIndex::index_to_piece_;
Array<Square, 2110> PsqIndex::index_to_square_;

ArrayMap<Array<PsqPair, 19>, Color, PieceType> PsqPair::hand_;
ArrayMap<PsqPair, Square, Piece> PsqPair::psq_;
ArrayMap<PsqPair, PsqIndex> PsqPair::all_pairs_;

void PsqIndex::Init() {
  // 1. 持ち駒のインデックスを計算する
  PsqIndex hand_index(0);
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : {kPawn, kLance, kKnight, kSilver, kGold, kBishop, kRook}) {
      // a. 持ち駒のインデックスのオフセットを計算する
      // ここで-1しているのは、持ち駒0枚目はカウントしないので、持ち駒が1枚目のときに、
      // インデックスが0から始まるようにするため。
      // このオフセットに持ち駒の枚数を足したものが、実際のインデックスとなる。
      // 例えば、先手の歩の3枚目の持ち駒であれば、そのインデックスは、-1+3=2となる。
      hand_[c][pt] = hand_index - 1;

      // b. 逆引き用の表も用意する
      for (int num = 1; num <= GetMaxNumber(pt); ++num) {
        index_to_square_[hand_index] = kSquareNone;
        index_to_piece_[hand_index] = Piece(c, pt);
        ++hand_index;
      }
    }
  }
  assert(hand_index == 76);

  // 2. 盤上の駒のインデックスを計算する
  PsqIndex board_index(76);
  for (Piece piece : Piece::all_pieces()) {
    if (piece.is(kKing)) {
      continue;
    }
    for (Square square : Square::all_squares()) {
      if (!piece.may_not_be_placed_on(square.rank())) {
        index_to_piece_[board_index] = piece;
        index_to_square_[board_index] = square;
        psq_[square][piece] = board_index++;
      }
    }
  }
  assert(board_index == PsqIndex::max() + 1);
}

void PsqPair::Init() {
  // PsqIndexの初期化を先に行う。
  PsqIndex::Init();

  // 1. 持ち駒
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      for (int num_pieces = 1; num_pieces <= GetMaxNumber(pt); ++num_pieces) {
        PsqIndex index_black = PsqIndex::OfHand( c, pt, num_pieces);
        PsqIndex index_white = PsqIndex::OfHand(~c, pt, num_pieces); // 先後反転
        PsqPair psq_pair(index_black, index_white);
        hand_[c][pt][num_pieces] = psq_pair;
        all_pairs_[psq_pair.black()] = psq_pair;
      }
    }
  }

  // 2. 盤上の駒
  for (Square s : Square::all_squares()) {
    for (Piece p : Piece::all_pieces()) {
      if (p.is(kKing) || p.may_not_be_placed_on(s.rank())) {
        continue;
      }
      PsqIndex index_black = PsqIndex::OfBoard(p, s);
      PsqIndex index_white = PsqIndex::OfBoard(p.opponent_piece(), Square::rotate180(s)); // 先後反転
      PsqPair psq_pair(index_black, index_white);
      psq_[s][p] = psq_pair;
      all_pairs_[psq_pair.black()] = psq_pair;
    }
  }
}

PsqList::PsqList(const Position& pos) : size_(0) {
  // 1. 持ち駒をコピーする
  // 持ち駒の枚数の情報をリスト自身が保持することにより、MakeMove(), UnmakeMove()の両関数に
  // Positionクラスを引数として渡す必要がなくなる。
  hand_[kBlack] = pos.hand(kBlack);
  hand_[kWhite] = pos.hand(kWhite);

  // 2. 持ち駒のインデックスを追加
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      for (int i = 1, n = pos.hand(c).count(pt); i <= n; ++i) {
        list_[size_] = PsqPair::OfHand(c, pt, i);
        hand_index_[c][pt][i] = size_;
        ++size_;
      }
    }
  }

  // 3. 盤上の駒のインデックスを追加
  pos.pieces().andnot(pos.pieces(kKing)).Serialize([&](Square s) {
    Piece piece = pos.piece_on(s);
    list_[size_] = PsqPair::OfBoard(piece, s);
    index_[s] = size_;
    ++size_;
  });

  assert(IsOk());
}

#if !defined(EVAL_NNUE)
void PsqList::MakeMove(const Move move) {
#else
void PsqList::MakeMove(const Move move, Eval::DirtyPiece& dp) {
#endif

  assert(move.IsOk());
  assert(move.is_real_move());

  Square to = move.to();
  Piece piece = move.piece();
  Color side_to_move = piece.color();

#if defined(EVAL_NNUE)
  dp.dirty_num = move.is_capture() ? 2 : 1;
#endif

  if (move.is_drop()) {
    // 打つ手の場合：移動元となった駒台のインデックスを、移動先のマスのインデックスで上書きする
    PieceType pt = piece.type();
    int num = hand_[side_to_move].count(pt);
    int idx = hand_index_[side_to_move][pt][num];
    list_[idx] = PsqPair::OfBoard(piece, to);
    index_[to] = idx;
    hand_[side_to_move].remove_one(pt);

#if defined(EVAL_NNUE)
    // 現在の実装では、dp.pieceNoは玉の場合にのみ使用している。
    dp.pieceNo[0] = PIECE_NUMBER_ZERO;

    PsqPair old_pair = PsqPair::OfHand(side_to_move, pt, num);
    dp.changed_piece[0].old_piece.fb = GetNnuePsqIndex(old_pair.black());
    dp.changed_piece[0].old_piece.fw = GetNnuePsqIndex(old_pair.white());

    PsqPair new_pair = PsqPair::OfBoard(piece, to);
    dp.changed_piece[0].new_piece.fb = GetNnuePsqIndex(new_pair.black());
    dp.changed_piece[0].new_piece.fw = GetNnuePsqIndex(new_pair.white());
#endif

  } else {
    Square from = move.from();

    // 取る手の場合：取った駒は、盤上から駒台に移動するので、新たなインデックスで上書きする
    if (move.is_capture()) {
      PieceType hand_type = move.captured_piece().hand_type();
      int num = hand_[side_to_move].count(hand_type) + 1;
      int idx = index_[to];
      list_[idx] = PsqPair::OfHand(side_to_move, hand_type, num);
      hand_index_[side_to_move][hand_type][num] = idx;
      hand_[side_to_move].add_one(hand_type);

#if defined(EVAL_NNUE)
      // 現在の実装では、dp.pieceNoは玉の場合にのみ使用している。
      dp.pieceNo[1] = PIECE_NUMBER_ZERO;

      PsqPair old_pair = PsqPair::OfBoard(move.captured_piece(), to);
      dp.changed_piece[1].old_piece.fb = GetNnuePsqIndex(old_pair.black());
      dp.changed_piece[1].old_piece.fw = GetNnuePsqIndex(old_pair.white());

      PsqPair new_pair = PsqPair::OfHand(side_to_move, hand_type, num);
      dp.changed_piece[1].new_piece.fb = GetNnuePsqIndex(new_pair.black());
      dp.changed_piece[1].new_piece.fw = GetNnuePsqIndex(new_pair.white());
#endif
    }

    // 動かす手の場合：移動元のインデックスを、移動先のインデックスで上書きする
    if (!piece.is(kKing)) { // 玉を動かす手の場合は、インデックスに変化はない
      int idx = index_[from];
      list_[idx] = PsqPair::OfBoard(move.piece_after_move(), to);
      index_[to] = idx;

#if defined(EVAL_NNUE)
      // 現在の実装では、dp.pieceNoは玉の場合にのみ使用している。
      dp.pieceNo[0] = PIECE_NUMBER_ZERO;

      PsqPair old_pair = PsqPair::OfBoard(piece, from);
      dp.changed_piece[0].old_piece.fb = GetNnuePsqIndex(old_pair.black());
      dp.changed_piece[0].old_piece.fw = GetNnuePsqIndex(old_pair.white());

      PsqPair new_pair = PsqPair::OfBoard(move.piece_after_move(), to);
      dp.changed_piece[0].new_piece.fb = GetNnuePsqIndex(new_pair.black());
      dp.changed_piece[0].new_piece.fw = GetNnuePsqIndex(new_pair.white());
#endif
    }
#if defined(EVAL_NNUE)
    else {
        // 現在の実装では、dp.pieceNoは玉の場合にのみ使用している。
        dp.pieceNo[0] = piece.color() == kBlack ? PIECE_NUMBER_BKING : PIECE_NUMBER_WKING;
    }
#endif

  }

  assert(IsOk());
}

void PsqList::UnmakeMove(const Move move) {
  assert(move.IsOk());
  assert(move.is_real_move());

  Square to = move.to();
  Piece piece = move.piece();
  Color side_to_move = piece.color();

  if (move.is_drop()) {
    // 打つ手の場合：移動先（盤上）のインデックスを、移動元（駒台）のインデックスに戻す
    PieceType pt = piece.type();
    int num = hand_[side_to_move].count(pt) + 1;
    int idx = index_[to];
    list_[idx] = PsqPair::OfHand(side_to_move, pt, num);
    hand_index_[side_to_move][pt][num] = idx;
    hand_[side_to_move].add_one(pt);
  } else {
    Square from = move.from();

    // 動かす手の場合：移動先のマスのインデックスを、移動元のインデックスに戻す
    if (!piece.is(kKing)) { // 玉を動かす手の場合は、インデックスに変化はない
      int idx = index_[to];
      list_[idx] = PsqPair::OfBoard(piece, from);
      index_[from] = idx;
    }

    // 取る手の場合：取った駒について、駒台のインデックスを盤上の元あった位置のインデックスに戻す
    if (move.is_capture()) {
      Piece captured = move.captured_piece();
      PieceType hand_type = captured.hand_type();
      int num = hand_[side_to_move].count(hand_type);
      int idx = hand_index_[side_to_move][hand_type][num];
      list_[idx] = PsqPair::OfBoard(captured, to);
      index_[to] = idx;
      hand_[side_to_move].remove_one(hand_type);
    }
  }

  assert(IsOk());
}

bool PsqList::IsOk() const {
  // 1. 要素数のチェック
  if (size_ > kMaxSize) {
    return false;
  }

  // 2. 持ち駒の要素がリストに含まれているかをチェックする
  for (Color c : {kBlack, kWhite}) {
    for (PieceType pt : Piece::all_hand_types()) {
      for (int num = 1; num <= hand_[c].count(pt); ++num) {
        // 持ち駒の要素がリストに含まれているか探す
        auto iter = std::find_if(begin(), end(), [&](PsqPair item) {
          return item.black() == PsqIndex::OfHand(c, pt, num);
        });
        // a. あるべき要素がリストになかった
        if (iter == end()) return false;
        // b. hand_index_との整合性がとれていなかった
        if (hand_index_[c][pt][num] != (iter - begin())) return false;
      }
    }
  }

  // 3. index_が正しい場所を示しているかチェック
  for (size_t i = 0; i < size_; ++i) {
    PsqPair item = list_[i];
    if (item.square() == kSquareNone) {
      continue;
    }
    if (index_[item.square()] != static_cast<int>(i)) {
      return false;
    }
  }

  return true;
}

bool PsqList::TwoListsHaveSameItems(const PsqList& list1, const PsqList& list2) {
  if (list1.size() != list2.size()) {
    return false;
  }

  // ローカル変数にコピーしたうえで、ソートする
  Array<PsqPair, kMaxSize> list_l = list1.list_, list_r = list2.list_;
  auto less = [](PsqPair lhs, PsqPair rhs) {
    return lhs.black() < rhs.black();
  };
  size_t size = list_l.size();
  std::sort(list_l.begin(), list_l.begin() + size, less);
  std::sort(list_r.begin(), list_r.begin() + size, less);

  // 双方を比較する
  return std::includes(list_l.begin(), list_l.begin() + size,
                       list_r.begin(), list_r.begin() + size, less);
}

constexpr PsqControlIndex::Key PsqControlIndex::kKeyPiece;
constexpr PsqControlIndex::Key PsqControlIndex::kKeyBlackControls;
constexpr PsqControlIndex::Key PsqControlIndex::kKeyWhiteControls;
constexpr PsqControlIndex::Key PsqControlIndex::kKeySquare;

PsqControlList::BitSet128 PsqControlList::ComputeDifference(const PsqControlList& lhs,
                                                            const PsqControlList& rhs) {
  BitSet128 bitset;

  constexpr uint8_t r = 0x80;
  const __m128i kMask = _mm_set_epi8(r, r, r, r, r, r, r, r, 14, 12, 10, 8, 6, 4, 2, 0);
  const __m128i kAllOne = _mm_set1_epi8(static_cast<int8_t>(UINT8_MAX));

  for (size_t i = 0; i < 11; ++i) {
    // 1. ２つのリストが同じかを比較する（これにより、2つのリストが同じ部分だけビットが１になる）
    __m128i equal = _mm_cmpeq_epi16(lhs.xmm(i), rhs.xmm(i));
    // 2. すべてのビットを反転する（これにより、2つのリストが異なっている部分だけビットが１になる）
    __m128i not_equal = _mm_andnot_si128(equal, kAllOne);
    // 3. 比較結果を下位64ビットに集約する
    __m128i shuffled = _mm_shuffle_epi8(not_equal, kMask);
    // 4. 結果をビットセットに保存する
    bitset.byte[i] = static_cast<uint8_t>(_mm_movemask_epi8(shuffled));
  }

  return bitset;
}


/** NNUE評価関数用のPsqIndexへの変換テーブル. */
Eval::BonaPiece nnue_psq_index_array[2110];

Eval::BonaPiece GetNnuePsqIndex(PsqIndex psq_index) {
  return nnue_psq_index_array[psq_index];
}

void SetNnuePsqIndexArray(int gikou_index_start, int nnue_index_start, int cnt) {
  int gikou_index = gikou_index_start;
  int nnue_index = nnue_index_start;

  for (int i = 0; i < cnt; i++) {
    nnue_psq_index_array[gikou_index] = (Eval::BonaPiece)nnue_index;

    gikou_index++;
    nnue_index++;
  }
}

void InitNnuePsqIndexArray() {

  // 技巧とNNUEのPsqIndexの持ち方の違いに注意しながら、変換テーブルを初期化する。
  // ・技巧 ：0～2109
  // ・NNUE ：0～1547
  // ・技巧はインデックスに隙間なし、NNUEは隙間あり。
  // ・技巧は「と～成銀」を「金」と区別するが、NNUEは区別しない。
  // ・技巧は「行きどころのない駒」を除外しているが、NNUEは除外していない。
  // ・盤上の駒の順序が異なる。


  // ----- 持ち駒
  SetNnuePsqIndexArray(0, 1, 18);
  SetNnuePsqIndexArray(18, 39, 4);
  SetNnuePsqIndexArray(22, 49, 4);
  SetNnuePsqIndexArray(26, 59, 4);
  SetNnuePsqIndexArray(30, 69, 4);
  SetNnuePsqIndexArray(34, 79, 2);
  SetNnuePsqIndexArray(36, 85, 2);
  SetNnuePsqIndexArray(38, 20, 18);
  SetNnuePsqIndexArray(56, 44, 4);
  SetNnuePsqIndexArray(60, 54, 4);
  SetNnuePsqIndexArray(64, 64, 4);
  SetNnuePsqIndexArray(68, 74, 4);
  SetNnuePsqIndexArray(72, 82, 2);
  SetNnuePsqIndexArray(74, 88, 2);


  // ----- 盤上の駒

  // 先手の歩
  SetNnuePsqIndexArray(76, 91, 8);
  SetNnuePsqIndexArray(84, 100, 8);
  SetNnuePsqIndexArray(92, 109, 8);
  SetNnuePsqIndexArray(100, 118, 8);
  SetNnuePsqIndexArray(108, 127, 8);
  SetNnuePsqIndexArray(116, 136, 8);
  SetNnuePsqIndexArray(124, 145, 8);
  SetNnuePsqIndexArray(132, 154, 8);
  SetNnuePsqIndexArray(140, 163, 8);

  // 先手の香
  SetNnuePsqIndexArray(148, 253, 8);
  SetNnuePsqIndexArray(156, 262, 8);
  SetNnuePsqIndexArray(164, 271, 8);
  SetNnuePsqIndexArray(172, 280, 8);
  SetNnuePsqIndexArray(180, 289, 8);
  SetNnuePsqIndexArray(188, 298, 8);
  SetNnuePsqIndexArray(196, 307, 8);
  SetNnuePsqIndexArray(204, 316, 8);
  SetNnuePsqIndexArray(212, 325, 8);

  // 先手の桂
  SetNnuePsqIndexArray(220, 416, 7);
  SetNnuePsqIndexArray(227, 425, 7);
  SetNnuePsqIndexArray(234, 434, 7);
  SetNnuePsqIndexArray(241, 443, 7);
  SetNnuePsqIndexArray(248, 452, 7);
  SetNnuePsqIndexArray(255, 461, 7);
  SetNnuePsqIndexArray(262, 470, 7);
  SetNnuePsqIndexArray(269, 479, 7);
  SetNnuePsqIndexArray(276, 488, 7);

  // 先手の銀～飛、と～龍
  SetNnuePsqIndexArray(283, 576, 81);
  SetNnuePsqIndexArray(364, 738, 81);
  SetNnuePsqIndexArray(445, 900, 81);
  SetNnuePsqIndexArray(526, 1224, 81);
  SetNnuePsqIndexArray(607, 738, 81);
  SetNnuePsqIndexArray(688, 738, 81);
  SetNnuePsqIndexArray(769, 738, 81);
  SetNnuePsqIndexArray(850, 738, 81);
  SetNnuePsqIndexArray(931, 1062, 81);
  SetNnuePsqIndexArray(1012, 1386, 81);

  // 後手の歩
  SetNnuePsqIndexArray(1093, 171, 8);
  SetNnuePsqIndexArray(1101, 180, 8);
  SetNnuePsqIndexArray(1109, 189, 8);
  SetNnuePsqIndexArray(1117, 198, 8);
  SetNnuePsqIndexArray(1125, 207, 8);
  SetNnuePsqIndexArray(1133, 216, 8);
  SetNnuePsqIndexArray(1141, 225, 8);
  SetNnuePsqIndexArray(1149, 234, 8);
  SetNnuePsqIndexArray(1157, 243, 8);

  // 後手の香
  SetNnuePsqIndexArray(1165, 333, 8);
  SetNnuePsqIndexArray(1173, 342, 8);
  SetNnuePsqIndexArray(1181, 351, 8);
  SetNnuePsqIndexArray(1189, 360, 8);
  SetNnuePsqIndexArray(1197, 369, 8);
  SetNnuePsqIndexArray(1205, 378, 8);
  SetNnuePsqIndexArray(1213, 387, 8);
  SetNnuePsqIndexArray(1221, 396, 8);
  SetNnuePsqIndexArray(1229, 405, 8);

  // 後手の桂
  SetNnuePsqIndexArray(1237, 495, 7);
  SetNnuePsqIndexArray(1244, 504, 7);
  SetNnuePsqIndexArray(1251, 513, 7);
  SetNnuePsqIndexArray(1258, 522, 7);
  SetNnuePsqIndexArray(1265, 531, 7);
  SetNnuePsqIndexArray(1272, 540, 7);
  SetNnuePsqIndexArray(1279, 549, 7);
  SetNnuePsqIndexArray(1286, 558, 7);
  SetNnuePsqIndexArray(1293, 567, 7);

  // 後手の銀～飛、と～龍
  SetNnuePsqIndexArray(1300, 657, 81);
  SetNnuePsqIndexArray(1381, 819, 81);
  SetNnuePsqIndexArray(1462, 981, 81);
  SetNnuePsqIndexArray(1543, 1305, 81);
  SetNnuePsqIndexArray(1624, 819, 81);
  SetNnuePsqIndexArray(1705, 819, 81);
  SetNnuePsqIndexArray(1786, 819, 81);
  SetNnuePsqIndexArray(1867, 819, 81);
  SetNnuePsqIndexArray(1948, 1143, 81);
  SetNnuePsqIndexArray(2029, 1467, 81);

}

