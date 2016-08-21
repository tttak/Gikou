/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016 Yosuke Demura
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

void PsqList::MakeMove(const Move move) {
  assert(move.IsOk());
  assert(move.is_real_move());

  Square to = move.to();
  Piece piece = move.piece();
  Color side_to_move = piece.color();

  if (move.is_drop()) {
    // 打つ手の場合：移動元となった駒台のインデックスを、移動先のマスのインデックスで上書きする
    PieceType pt = piece.type();
    int num = hand_[side_to_move].count(pt);
    int idx = hand_index_[side_to_move][pt][num];
    list_[idx] = PsqPair::OfBoard(piece, to);
    index_[to] = idx;
    hand_[side_to_move].remove_one(pt);
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
    }

    // 動かす手の場合：移動元のインデックスを、移動先のインデックスで上書きする
    if (!piece.is(kKing)) { // 玉を動かす手の場合は、インデックスに変化はない
      int idx = index_[from];
      list_[idx] = PsqPair::OfBoard(move.piece_after_move(), to);
      index_[to] = idx;
    }
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


/** nozomi評価関数用のPsqIndexへの変換テーブル. */
int nozomi_psq_index_array[2110];

int GetNozomiPsqIndex(PsqIndex psq_index) {
  return nozomi_psq_index_array[psq_index];
}

int GetNozomiSquare(int file, int rank) {
  return -(file + 1) + (rank + 1) * 9;
}

int GetGikouSquare(int file, int rank) {
  return file * 9 + rank;
}

// -----
// Square内のインデックスを変換する。（nozomi→技巧）
// ・sq_nozomi：0～80
// ・戻り値   ：0～80
// -----
int GetGikouSquareFromNozomi_9x9(int sq_nozomi) {
  int file = 8 - sq_nozomi % 9;
  int rank = sq_nozomi / 9;

  return GetGikouSquare(file, rank);
}

// -----
// Square内のインデックスを変換する。（技巧→nozomi）
// ・sq_gikou：0～80
// ・戻り値  ：0～80
// -----
// ・技巧は  ｛右上（1一）が0、右下（1九）が8、左上（9一）が72、左下（9九）が80｝。
// ・nozomiは｛左上（9一）が0、右上（1一）が8、左下（9九）が72、右下（1九）が80｝。
// （例）72→0、0→8、73→9、1→17、74→18、2→26、80→72、8→80
// -----
int GetNozomiSquareFromGikou_9x9(int sq_gikou) {
  int file = sq_gikou / 9;
  int rank = sq_gikou % 9;

  return GetNozomiSquare(file, rank);
}

// -----
// Square内のインデックスを変換する。（技巧→nozomi）
// ・sq_gikou：0～71（1段目を除く）
// ・戻り値  ：9～80（1段目を除く）
// （例）（1二）0→17、（1九）7→80、（9二）64→9、（9九）71→72
// -----
int GetNozomiSquareFromGikou_9x8_black(int sq_gikou) {
  int file = sq_gikou / 8;
  int rank = sq_gikou % 8 + 1;

  return GetNozomiSquare(file, rank);
}

// -----
// Square内のインデックスを変換する。（技巧→nozomi）
// ・sq_gikou： 0～62（1段目、2段目を除く）
// ・戻り値  ：18～80（1段目、2段目を除く）
// （例）（1三）0→26、（1九）6→80、（9三）56→18、（9九）62→72
// -----
int GetNozomiSquareFromGikou_9x7_black(int sq_gikou) {
  int file = sq_gikou / 7;
  int rank = sq_gikou % 7 + 2;

  return GetNozomiSquare(file, rank);
}

// -----
// Square内のインデックスを変換する。（技巧→nozomi）
// ・sq_gikou：0～71（9段目を除く）
// ・戻り値  ：0～71（9段目を除く）
// （例）（1一）0→8、（1八）7→71、（9一）64→0、（9八）71→63
// -----
int GetNozomiSquareFromGikou_9x8_white(int sq_gikou) {
  int file = sq_gikou / 8;
  int rank = sq_gikou % 8;

  return GetNozomiSquare(file, rank);
}

// -----
// Square内のインデックスを変換する。（技巧→nozomi）
// ・sq_gikou：0～62（8段目、9段目を除く）
// ・戻り値  ：0～62（8段目、9段目を除く）
// （例）（1一）0→8、（1七）6→62、（9一）56→0、（9七）62→54
// -----
int GetNozomiSquareFromGikou_9x7_white(int sq_gikou) {
  int file = sq_gikou / 7;
  int rank = sq_gikou % 7;

  return GetNozomiSquare(file, rank);
}

void SetNozomiPsqIndexArray_normal(int gikou_index_start, int nozomi_index_start, int cnt) {
  int gikou_index = gikou_index_start;
  int nozomi_index = nozomi_index_start;

  for (int i = 0; i < cnt; i++) {
    nozomi_psq_index_array[gikou_index] = nozomi_index;

    gikou_index++;
    nozomi_index++;
  }
}

void SetNozomiPsqIndexArray_9x9(int gikou_index_start, int nozomi_index_start) {
  for (int sq_gikou = 0; sq_gikou < 81; sq_gikou++) {
    int sq_nozomi = GetNozomiSquareFromGikou_9x9(sq_gikou);

    nozomi_psq_index_array[gikou_index_start + sq_gikou] = nozomi_index_start + sq_nozomi;
  }
}

void SetNozomiPsqIndexArray_9x8_black(int gikou_index_start, int nozomi_index_start) {
  for (int sq_gikou = 0; sq_gikou < 72; sq_gikou++) {
    int sq_nozomi = GetNozomiSquareFromGikou_9x8_black(sq_gikou);

    nozomi_psq_index_array[gikou_index_start + sq_gikou] = nozomi_index_start + sq_nozomi;
  }
}

void SetNozomiPsqIndexArray_9x7_black(int gikou_index_start, int nozomi_index_start) {
  for (int sq_gikou = 0; sq_gikou < 63; sq_gikou++) {
    int sq_nozomi = GetNozomiSquareFromGikou_9x7_black(sq_gikou);

    nozomi_psq_index_array[gikou_index_start + sq_gikou] = nozomi_index_start + sq_nozomi;
  }
}

void SetNozomiPsqIndexArray_9x8_white(int gikou_index_start, int nozomi_index_start) {
  for (int sq_gikou = 0; sq_gikou < 72; sq_gikou++) {
    int sq_nozomi = GetNozomiSquareFromGikou_9x8_white(sq_gikou);

    nozomi_psq_index_array[gikou_index_start + sq_gikou] = nozomi_index_start + sq_nozomi;
  }
}

void SetNozomiPsqIndexArray_9x7_white(int gikou_index_start, int nozomi_index_start) {
  for (int sq_gikou = 0; sq_gikou < 63; sq_gikou++) {
    int sq_nozomi = GetNozomiSquareFromGikou_9x7_white(sq_gikou);

    nozomi_psq_index_array[gikou_index_start + sq_gikou] = nozomi_index_start + sq_nozomi;
  }
}

void InitNozomiPsqIndexArray() {

  // 技巧とnozomiのPsqIndexの持ち方の違いに注意しながら、変換テーブルを初期化する。
  // ・技巧：0～2109
  // ・nozomi：0～1547
  // ・技巧はインデックスに隙間なし、nozomiは隙間あり。
  // ・技巧は「と～成銀」を「金」と区別するが、nozomiは区別しない。
  // ・技巧は「行きどころのない駒」を除外しているが、nozomiは除外していない。
  // ・盤上の駒の順序が異なる。
  // ・Square内のインデックスが異なる。
  //   技巧は  ｛右上（1一）が0、右下（1九）が8、左上（9一）が72、左下（9九）が80｝。
  //   nozomiは｛左上（9一）が0、右上（1一）が8、左下（9九）が72、右下（1九）が80｝。
  //
  // ----- 技巧
  //  各マスには、整数値が割り当てられています。
  //  割り当てられている値は、将棋盤の右上（１一）が0で、左下（９九）が80です。
  //  具体的には、以下のように割り当てられています。
  //
  //    9  8  7  6  5  4  3  2  1
  // +----------------------------+
  // | 72 63 54 45 36 27 18  9  0 | 一
  // | 73 64 55 46 37 28 19 10  1 | 二
  // | 74 65 56 47 38 29 20 11  2 | 三
  // | 75 66 57 48 39 30 21 12  3 | 四
  // | 76 67 58 49 40 31 22 13  4 | 五
  // | 77 68 59 50 41 32 23 14  5 | 六
  // | 78 69 60 51 42 33 24 15  6 | 七
  // | 79 70 61 52 43 34 25 16  7 | 八
  // | 80 71 62 53 44 35 26 17  8 | 九
  // +----------------------------+
  //
  // ----- nozomi
  // enum Square
  // {
  //   k9A = 0, k8A, k7A, k6A, k5A, k4A, k3A, k2A, k1A,
  //   k9B, k8B, k7B, k6B, k5B, k4B, k3B, k2B, k1B,
  //   k9C, k8C, k7C, k6C, k5C, k4C, k3C, k2C, k1C,
  //   // x < k9Dの範囲が後手陣。先手にとって駒がなれるのはこの範囲。
  //   k9D, k8D, k7D, k6D, k5D, k4D, k3D, k2D, k1D,
  //   k9E, k8E, k7E, k6E, k5E, k4E, k3E, k2E, k1E,
  //   k9F, k8F, k7F, k6F, k5F, k4F, k3F, k2F, k1F,
  //   // x > k1Fの範囲が先手陣。後手にとって駒がなれるのはこの範囲。
  //   k9G, k8G, k7G, k6G, k5G, k4G, k3G, k2G, k1G,
  //   k9H, k8H, k7H, k6H, k5H, k4H, k3H, k2H, k1H,
  //   k9I, k8I, k7I, k6I, k5I, k4I, k3I, k2I, k1I,
  //   kBoardSquare,
  //   （以下、省略）
  // }
  // -----

  // ----- 持ち駒

  // 先手の歩～飛
  SetNozomiPsqIndexArray_normal(0, 1, 18);
  SetNozomiPsqIndexArray_normal(18, 39, 4);
  SetNozomiPsqIndexArray_normal(22, 49, 4);
  SetNozomiPsqIndexArray_normal(26, 59, 4);
  SetNozomiPsqIndexArray_normal(30, 69, 4);
  SetNozomiPsqIndexArray_normal(34, 79, 2);
  SetNozomiPsqIndexArray_normal(36, 85, 2);

  // 後手の歩～飛
  SetNozomiPsqIndexArray_normal(38, 20, 18);
  SetNozomiPsqIndexArray_normal(56, 44, 4);
  SetNozomiPsqIndexArray_normal(60, 54, 4);
  SetNozomiPsqIndexArray_normal(64, 64, 4);
  SetNozomiPsqIndexArray_normal(68, 74, 4);
  SetNozomiPsqIndexArray_normal(72, 82, 2);
  SetNozomiPsqIndexArray_normal(74, 88, 2);


  // ----- 盤上の駒

  // --- 先手

  // 先手の歩
  SetNozomiPsqIndexArray_9x8_black(76, 90);

  // 先手の香
  SetNozomiPsqIndexArray_9x8_black(148, 252);

  // 先手の桂
  SetNozomiPsqIndexArray_9x7_black(220, 414);

  // 先手の銀～飛
  SetNozomiPsqIndexArray_9x9(283, 576);
  SetNozomiPsqIndexArray_9x9(364, 738);
  SetNozomiPsqIndexArray_9x9(445, 900);
  SetNozomiPsqIndexArray_9x9(526, 1224);

  // 先手のと～龍
  SetNozomiPsqIndexArray_9x9(607, 738);
  SetNozomiPsqIndexArray_9x9(688, 738);
  SetNozomiPsqIndexArray_9x9(769, 738);
  SetNozomiPsqIndexArray_9x9(850, 738);
  SetNozomiPsqIndexArray_9x9(931, 1062);
  SetNozomiPsqIndexArray_9x9(1012, 1386);


  // --- 後手

  // 後手の歩
  SetNozomiPsqIndexArray_9x8_white(1093, 171);

  // 後手の香
  SetNozomiPsqIndexArray_9x8_white(1165, 333);

  // 後手の桂
  SetNozomiPsqIndexArray_9x7_white(1237, 495);

  // 後手の銀～飛
  SetNozomiPsqIndexArray_9x9(1300, 657);
  SetNozomiPsqIndexArray_9x9(1381, 819);
  SetNozomiPsqIndexArray_9x9(1462, 981);
  SetNozomiPsqIndexArray_9x9(1543, 1305);

  // 後手のと～龍
  SetNozomiPsqIndexArray_9x9(1624, 819);
  SetNozomiPsqIndexArray_9x9(1705, 819);
  SetNozomiPsqIndexArray_9x9(1786, 819);
  SetNozomiPsqIndexArray_9x9(1867, 819);
  SetNozomiPsqIndexArray_9x9(1948, 1143);
  SetNozomiPsqIndexArray_9x9(2029, 1467);

}

// psq関連のテスト（デバッグ用）
void TestPsq() {

  // ----- 9x9
  int sq_nozomi[8] = {  0, 8,  9, 17, 18, 26, 72, 80 };
  int sq_gikou [8] = { 72, 0, 73,  1, 74,  2, 80,  8 };

  for (int i = 0; i < 8; i++ ) {
    std::printf("GetGikouSquareFromNozomi_9x9(%d)=%d\n", sq_nozomi[i], GetGikouSquareFromNozomi_9x9(sq_nozomi[i]));
    std::printf("GetNozomiSquareFromGikou_9x9(%d)=%d\n", sq_gikou [i], GetNozomiSquareFromGikou_9x9(sq_gikou [i]));
  }


  // ----- 9x8_black
  int sq_gikou_9x8_black[4] = { 0, 7, 64, 71 };

  for (int i = 0; i < 4; i++ ) {
    std::printf("GetNozomiSquareFromGikou_9x8_black(%d)=%d\n", sq_gikou_9x8_black[i], GetNozomiSquareFromGikou_9x8_black(sq_gikou_9x8_black[i]));
  }


  // ----- 9x7_black
  int sq_gikou_9x7_black[4] = { 0, 6, 56, 62 };

  for (int i = 0; i < 4; i++ ) {
    std::printf("GetNozomiSquareFromGikou_9x7_black(%d)=%d\n", sq_gikou_9x7_black[i], GetNozomiSquareFromGikou_9x7_black(sq_gikou_9x7_black[i]));
  }


  // ----- 9x8_white
  int sq_gikou_9x8_white[4] = { 0, 7, 64, 71 };

  for (int i = 0; i < 4; i++ ) {
    std::printf("GetNozomiSquareFromGikou_9x8_white(%d)=%d\n", sq_gikou_9x8_white[i], GetNozomiSquareFromGikou_9x8_white(sq_gikou_9x8_white[i]));
  }


  // ----- 9x7_white
  int sq_gikou_9x7_white[4] = { 0, 6, 56, 62 };

  for (int i = 0; i < 4; i++ ) {
    std::printf("GetNozomiSquareFromGikou_9x7_white(%d)=%d\n", sq_gikou_9x7_white[i], GetNozomiSquareFromGikou_9x7_white(sq_gikou_9x7_white[i]));
  }

}
