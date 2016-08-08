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

#include "evaluation.h"

#include "common/arraymap.h"
#include "common/math.h"
#include "material.h"
#include "position.h"
#include "progress.h"

#include <fstream>
#include <iostream>

std::unique_ptr<EvalParameters> g_eval_params(new EvalParameters);

// やねうら王classicの評価値を混ぜる割合（序盤、中盤、終盤）（単位は%）
extern int g_YaneuraOuClassicEvalOpening;
extern int g_YaneuraOuClassicEvalMiddleGame;
extern int g_YaneuraOuClassicEvalEndGame;

// やねうら王classicの評価関数バイナリのフォルダ
extern std::string g_YaneuraOuClassicEvalFolder;

// やねうら王classicの評価関数テーブルの要素数など
const int SQ_NB_PLUS1 = 82;
const int fe_end = 1535;
const int fe_hand_end = 77;

// やねうら王classicの評価関数パラメータ用
typedef int32_t ValueKk;
typedef int32_t ValueKkp;
typedef int16_t ValueKpp;

// やねうら王classicの評価関数パラメータ（技巧のPsqIndexへの変換後）
ValueKk  kk_yaneuraou_classic [81][81];
ValueKkp kkp_yaneuraou_classic[81][81][2110];
ValueKpp kpp_yaneuraou_classic[81][2110][2110];


Score EvalDetail::ComputeFinalScore(Color side_to_move,
                                    double* const progress_output) const {

  PackedScore kp_total = kp[kBlack] + kp[kWhite];
  PackedScore others = controls + two_pieces + king_safety + sliders;
  int64_t sum = 0;

  // 1. 進行度を求める
  // 計算を高速化するため、進行度は整数型を使い、固定小数点で表す。
  constexpr int64_t kScale = Progress::kWeightScale;
  double weight = static_cast<double>(kp_total[3]);
  double progress_double = math::sigmoid(weight * double(1.0 / kScale));
  int64_t progress = static_cast<int64_t>(progress_double * kScale);
  if (progress_output != nullptr) {
    *progress_output = progress_double;
  }

  // 2. KPの内分を取る + 手番評価の内分を取る
  //    序盤=kp_sum[0]、中盤=kp_sum[1]、終盤=kp_sum[2]
  //    なお、進行度に応じて「評価値の内分をとる」ことの意味については、
  //        金子知適: 「GPS将棋」の評価関数とコンピュータ将棋による棋譜の検討,
  //        『コンピュータ将棋の進歩６』, pp.25-45, 2012.
  //    を参照してください。
  int64_t tempo;
  if (progress < (kScale / 2)) {
    int64_t opening     = -2 * progress + 1 * kScale;
    int64_t middle_game = +2 * progress             ;
    sum += (opening * kp_total[0]) + (middle_game * kp_total[1]);
    tempo = (opening * g_eval_params->tempo[0]) + (middle_game * g_eval_params->tempo[1]);
  } else {
    int64_t middle_game = -2 * progress + 2 * kScale;
    int64_t end_game    = +2 * progress - 1 * kScale;
    sum += (middle_game * kp_total[1]) + (end_game * kp_total[2]);
    tempo = (middle_game * g_eval_params->tempo[1]) + (end_game * g_eval_params->tempo[2]);
  }

  // 3. KP以外のパラメータについて内分を取る
  //    序盤=pp[0]、序盤の手番=pp[1]、終盤=pp[2]、終盤の手番=pp[3]
  if (side_to_move == kBlack) {
    int64_t opening  = (kScale - progress) * (others[0] + others[1] / 10);
    int64_t end_game = (progress         ) * (others[2] + others[3] / 10);
    sum += (opening + end_game);
    sum += tempo / 2;
  } else {
    int64_t opening  = (kScale - progress) * (others[0] - others[1] / 10);
    int64_t end_game = (progress         ) * (others[2] - others[3] / 10);
    sum += (opening + end_game);
    sum -= tempo / 2;
  }

  // 4. 小数点以下を切り捨てる
  //int64_t score = sum / (kScale * static_cast<int64_t>(kFvScale));
  //assert(-kScoreMaxEval <= score && score <= kScoreMaxEval);

  // 5. 後手番の場合は、得点を反転させる（常に手番側から見た点数になるようにする）
  //return static_cast<Score>(side_to_move == kBlack ? score : -score);


  // 技巧の評価値（double）
  double score_gikou = (double)sum / (kScale * static_cast<int64_t>(kFvScale));
  score_gikou = (side_to_move == kBlack ? score_gikou : -score_gikou);

  // やねうら王classicの評価値（double）
  double score_yaneuraou_classic = YaneuraOuClassicEval::ToCentiPawn(yaneuraou_classic_eval_detail.Sum(side_to_move));

  // 序盤・中盤・終盤の評価値の割合
  double rate_opening_yaneuraou_classic = (double)g_YaneuraOuClassicEvalOpening / 100;
  double rate_opening_gikou = 1 - rate_opening_yaneuraou_classic;

  double rate_middle_game_yaneuraou_classic = (double)g_YaneuraOuClassicEvalMiddleGame / 100;
  double rate_middle_game_gikou = 1 - rate_middle_game_yaneuraou_classic;

  double rate_end_game_yaneuraou_classic = (double)g_YaneuraOuClassicEvalEndGame / 100;
  double rate_end_game_gikou = 1 - rate_end_game_yaneuraou_classic;

  // 現在の進行度での技巧、やねうら王classicの評価値の割合
  double rate_gikou = 0;
  double rate_yaneuraou_classic = 0;

  if (progress_double < 0.5) {
    rate_gikou = (1 - progress_double * 2) * rate_opening_gikou + (progress_double * 2) * rate_middle_game_gikou;
    rate_yaneuraou_classic = (1 - progress_double * 2) * rate_opening_yaneuraou_classic + (progress_double * 2) * rate_middle_game_yaneuraou_classic;
  } else {
    rate_gikou = (2 - progress_double * 2) * rate_middle_game_gikou + (progress_double * 2 - 1) * rate_end_game_gikou;
    rate_yaneuraou_classic = (2 - progress_double * 2) * rate_middle_game_yaneuraou_classic + (progress_double * 2 - 1) * rate_end_game_yaneuraou_classic;
  }

  // 最終的な評価値
  int score_mix = score_gikou * rate_gikou + score_yaneuraou_classic * rate_yaneuraou_classic;
  score_mix = std::max(std::min(score_mix, (int)(kScoreMaxEval - 1)), (int)(- kScoreMaxEval + 1));

  return static_cast<Score>(score_mix);
}

namespace {

/**
 * 序盤・中盤・終盤の得点を、先後反転します.
 *
 * 進行度計算用の重みについては、先後反転されないことに注意してください。
 */
inline PackedScore FlipScores3x1(PackedScore s) {
  int32_t opening     = -s[0]; // 序盤
  int32_t middle_game = -s[1]; // 中盤
  int32_t end_game    = -s[2]; // 終盤
  int32_t progress    = s[3];  // 進行度計算用の重み
  return PackedScore(opening, middle_game, end_game, progress);
}

/**
 * 序盤・終盤の得点を、先後反転します.
 *
 * 手番の得点については、先後反転されないことに注意してください。
 */
inline PackedScore FlipScores2x2(PackedScore s) {
  int32_t opening        = -s[0]; // 序盤
  int32_t opening_tempo  = s[1];  // 手番（序盤用）
  int32_t end_game       = -s[2]; // 終盤
  int32_t end_game_tempo = s[3];  // 手番（終盤用）
  return PackedScore(opening, opening_tempo, end_game, end_game_tempo);
}

/**
 * 特定の１駒について、位置評価の合計値を計算します.
 */
inline EvalDetail SumPositionalScore(const PsqPair psq, const PsqList& list,
                                     const Position& pos) {
  // 1. KP
  Square bk = pos.king_square(kBlack);
  Square wk = Square::rotate180(pos.king_square(kWhite));
  PackedScore kp_black = g_eval_params->king_piece[bk][psq.black()];
  PackedScore kp_white = g_eval_params->king_piece[wk][psq.white()];

  // 2. PP
  PackedScore two_pieces(0);
  for (const PsqPair& i : list) {
    two_pieces += g_eval_params->two_pieces[psq.black()][i.black()];
  }


  // 3. やねうら王classicのKKPとKPP
  // ・KKは計算不要。この「特定の１駒」は玉ではないので。
  YaneuraOuClassicEvalDetail yaneuraou_classic_eval_detail;

  // KKP
  yaneuraou_classic_eval_detail.kkp_board = kkp_yaneuraou_classic[bk][wk][psq.black()];

  for (const PsqPair& j : list) {
    if (j.black() != psq.black()) {
      // KPP
      yaneuraou_classic_eval_detail.kpp_board[kBlack] += kpp_yaneuraou_classic[bk][psq.black()][j.black()];
      yaneuraou_classic_eval_detail.kpp_board[kWhite] += kpp_yaneuraou_classic[wk][psq.white()][j.white()];
    }
  }

  // -----

  EvalDetail sum;
  sum.kp[kBlack] = kp_black;
  sum.kp[kWhite] = FlipScores3x1(kp_white);
  sum.two_pieces = two_pieces;
  sum.yaneuraou_classic_eval_detail = yaneuraou_classic_eval_detail;

  return sum;
}

/**
 * 特定の２駒について、位置評価の合計値を計算します.
 */
inline EvalDetail SumPositionalScore(const PsqPair psq1, const PsqPair psq2,
                                     const PsqList& list, const Position& pos) {
  // 1. KP
  Square bk = pos.king_square(kBlack);
  Square wk = Square::rotate180(pos.king_square(kWhite));
  PackedScore kp_black = g_eval_params->king_piece[bk][psq1.black()]
                       + g_eval_params->king_piece[bk][psq2.black()];
  PackedScore kp_white = g_eval_params->king_piece[wk][psq1.white()]
                       + g_eval_params->king_piece[wk][psq2.white()];

  // 2. PP
  PackedScore two_pieces(0);
  for (const PsqPair& i : list) {
    two_pieces += g_eval_params->two_pieces[psq1.black()][i.black()];
    two_pieces += g_eval_params->two_pieces[psq2.black()][i.black()];
  }

  // 3. PP計算で重複して加算されてしまった部分を補正する
  two_pieces -= g_eval_params->two_pieces[psq1.black()][psq2.black()];


  // 4. やねうら王classicのKKPとKPP
  // ・KKは計算不要。この「特定の２駒」は玉ではないので。
  YaneuraOuClassicEvalDetail yaneuraou_classic_eval_detail;

  // KKP
  yaneuraou_classic_eval_detail.kkp_board = kkp_yaneuraou_classic[bk][wk][psq1.black()]
                                          + kkp_yaneuraou_classic[bk][wk][psq2.black()];

  // 駒１のKPP
  for (const PsqPair& j : list) {
    if (j.black() != psq1.black()) {
      // KPP
      yaneuraou_classic_eval_detail.kpp_board[kBlack] += kpp_yaneuraou_classic[bk][psq1.black()][j.black()];
      yaneuraou_classic_eval_detail.kpp_board[kWhite] += kpp_yaneuraou_classic[wk][psq1.white()][j.white()];
    }
  }

  // 駒２のKPP
  for (const PsqPair& j : list) {
    if (j.black() != psq2.black()) {
      // KPP
      yaneuraou_classic_eval_detail.kpp_board[kBlack] += kpp_yaneuraou_classic[bk][psq2.black()][j.black()];
      yaneuraou_classic_eval_detail.kpp_board[kWhite] += kpp_yaneuraou_classic[wk][psq2.white()][j.white()];
    }
  }


  // 5. やねうら王classicのKPP計算で重複して加算されてしまった部分を補正する
  yaneuraou_classic_eval_detail.kpp_board[kBlack] -= kpp_yaneuraou_classic[bk][psq1.black()][psq2.black()];
  yaneuraou_classic_eval_detail.kpp_board[kWhite] -= kpp_yaneuraou_classic[wk][psq1.white()][psq2.white()];


  // -----

  EvalDetail sum;
  sum.kp[kBlack] = kp_black;
  sum.kp[kWhite] = FlipScores3x1(kp_white);
  sum.two_pieces = two_pieces;
  sum.yaneuraou_classic_eval_detail = yaneuraou_classic_eval_detail;

  return sum;
}

/**
 * すべての駒について、位置評価の評価値を計算します.
 */
inline EvalDetail EvaluatePositionalAdvantage(const Position& pos,
                                              const PsqList& list) {
  const Square bk = pos.king_square(kBlack);
  const Square wk = Square::rotate180(pos.king_square(kWhite));

  PackedScore kp_black(0), kp_white(0), two_pieces(0);
  for (const PsqPair* i = list.begin(); i != list.end(); ++i) {
    // 1. KP
    kp_black += g_eval_params->king_piece[bk][i->black()];
    kp_white += g_eval_params->king_piece[wk][i->white()];
    // 2. PP
    for (const PsqPair* j = list.begin(); j <= i; ++j) {
      two_pieces += g_eval_params->two_pieces[i->black()][j->black()];
    }
  }

  EvalDetail sum;
  sum.kp[kBlack] = kp_black;
  sum.kp[kWhite] = FlipScores3x1(kp_white);
  sum.two_pieces = two_pieces;

  return sum;
}

/**
 * 各マスの利きについて、評価値の合計を求めます.
 *
 * （参考文献）
 *   - 竹内章: 習甦の誕生, 『人間に勝つコンピュータ将棋の作り方』, pp.171-190, 技術評論社, 2012.
 */
PackedScore EvaluateControls(const Position& pos, const PsqControlList& list) {
  PackedScore sum(0);
  const Square bk = pos.king_square(kBlack);
  const Square wk = pos.king_square(kWhite);

  for (const Square s : Square::all_squares()) {
    PsqControlIndex index = list[s];
    // 1. 先手玉との関係
    sum += g_eval_params->controls[kBlack][bk][index];
    // 2. 後手玉との関係
    // 注：KPとは異なり、インデックスの反転処理に時間がかかるため、インデックスと符号の反転処理は行わず、
    // 先手玉用・後手玉用の２つのテーブルを用意することで対応している。
    // その代わり、次元下げを行う段階で、インデックスと符号の反転処理を行っている。
    sum += g_eval_params->controls[kWhite][wk][index];
  }

  return sum;
}

/**
 * 各マスの利きについて、評価値を差分計算します.
 */
PackedScore EvaluateDifferenceOfControls(const Position& pos,
                                         const PsqControlList& previous_list,
                                         const PsqControlList& current_list) {

  PackedScore diff(0);
  const Square bk = pos.king_square(kBlack);
  const Square wk = pos.king_square(kWhite);

  // 前の局面と、利きや駒の位置が変化したマスを調べる
  const auto difference = PsqControlList::ComputeDifference(previous_list, current_list);

  // 前の局面との差分のみ、更新する
  difference.ForEach([&](Square sq) {
    // 1. 古い特徴を削除する
    PsqControlIndex old_index = previous_list[sq];
    diff -= g_eval_params->controls[kBlack][bk][old_index];
    diff -= g_eval_params->controls[kWhite][wk][old_index];
    // 2. 新しい特徴を追加する
    PsqControlIndex new_index = current_list[sq];
    diff += g_eval_params->controls[kBlack][bk][new_index];
    diff += g_eval_params->controls[kWhite][wk][new_index];
  });

  return diff;
}

/**
 * 玉の安全度に関する評価値を計算します.
 *
 * 玉の安全度を評価する際には、
 *   - 玉の上部に相手の利きがあると危険性が高い（将棋の駒は前に進める駒が多いため）
 *   - 相手の持ち駒の多寡で、玉の危険度が変化する
 *   - 穴熊か否かは玉の自由度に大きく影響するので区別する
 * といったことを考慮しています（idea from YSS、激指、TACOS）。
 *
 * （参考文献）
 *   - 山下宏: YSS--そのデータ構造、およびアルゴリズムについて, 『コンピュータ将棋の進歩２』,
 *     p.127, 共立出版, 1998.
 *   - 鶴岡慶雅: 将棋プログラム「激指」, 『コンピュータ将棋の進歩３』, pp.10-11, 共立出版,
 *     2003.
 *   - 橋本剛: 将棋プログラムTACOSのアルゴリズム, 『コンピュータ将棋の進歩５』, pp.53-55,
 *     共立出版, 2005.
 */
template<Color kKingColor, bool kMirrorHorizontally>
FORCE_INLINE PackedScore EvaluateKingSafety(const Position& pos) {
  assert(pos.king_square(kKingColor).relative_square(kKingColor).file() >= kFile5 || kMirrorHorizontally);

  const Square ksq = pos.king_square(kKingColor);

  // 1. 相手の持ち駒のbit setを取得する
  HandSet hs = pos.hand(~kKingColor).GetHandSet();

  // 2. 穴熊かどうかを調べる
  Square rksq = ksq.relative_square(kKingColor);
  bool is_anaguma = (rksq == kSquare9I) || (rksq == kSquare1I);
  // Hack: 持ち駒のbit setの1ビット目は未使用なので、穴熊か否かのフラグをbit setの1ビット目に立てておく
  hs.set(kNoPieceType, is_anaguma);

  // 3. 玉の周囲8マスの利き数と駒を取得する
  const ExtendedBoard& eb = pos.extended_board();
  EightNeighborhoods attacks = eb.GetEightNeighborhoodControls(~kKingColor, ksq);
  EightNeighborhoods defenses = eb.GetEightNeighborhoodControls(kKingColor, ksq);
  EightNeighborhoods pieces = eb.GetEightNeighborhoodPieces(ksq);
  // 攻め方の利き数については、利き数の最大値を3に制限する
  attacks = attacks.LimitTo(3);
  // 受け方の利き数については、玉の利きを除外したうえで、最大値を3に制限する
  defenses = defenses.Subtract(1).LimitTo(3);

  // 4. 評価値テーブルを参照するためのラムダ関数を準備する
  auto look_up = [&](Direction dir) -> PackedScore {
    // 後手玉の場合は、将棋盤を180度反転させて、先手番の玉として評価する
    Direction dir_i = kKingColor == kBlack ? dir : inverse_direction(dir);
    // 玉が右側にいる場合は、左右反転させて、将棋盤の左側にあるものとして評価する
    Direction dir_m = kMirrorHorizontally ? mirror_horizontally(dir_i) : dir_i;
    // 盤上にある駒を取得する
    Piece piece(pieces.at(dir_m));
    assert(piece.IsOk());
    // 後手玉を評価する場合は、周囲８マスの駒の先後を入れ替える
    if (kKingColor == kWhite && !piece.is(kNoPieceType)) {
      piece = piece.opponent_piece();
    }
    // 利き数を取得する
    int attackers = attacks.at(dir_m);
    int defenders = defenses.at(dir_m);
    // テーブルから評価値を参照する
    return g_eval_params->king_safety[hs][dir][piece][attackers][defenders];
  };

  // 5. 玉の周囲8マスについて、玉の安全度評価の合計値を求める
  PackedScore sum(0);
  sum += look_up(kDirNE);
  sum += look_up(kDirE );
  sum += look_up(kDirSE);
  sum += look_up(kDirN );
  sum += look_up(kDirS );
  sum += look_up(kDirNW);
  sum += look_up(kDirW );
  sum += look_up(kDirSW);

  // 後手番の場合は、評価値の符号を反転させる
  return kKingColor == kBlack ? sum : FlipScores2x2(sum);
}

template<Color kKingColor>
PackedScore EvaluateKingSafety(const Position& pos) {
  // 玉が右側にいる場合は、左右反転させて、将棋盤の左側にあるものとして評価する
  // これにより、「玉の左か右か」という観点でなく、「盤の端か中央か」という観点での評価を行うことができる
  if (pos.king_square(kKingColor).relative_square(kKingColor).file() <= kFile4) {
    return EvaluateKingSafety<kKingColor, true>(pos);
  } else {
    return EvaluateKingSafety<kKingColor, false>(pos);
  }
}

inline PackedScore EvaluateKingSafety(const Position& pos) {
  PackedScore score_black = EvaluateKingSafety<kBlack>(pos);
  PackedScore score_white = EvaluateKingSafety<kWhite>(pos);
  return score_black + score_white;
}

/**
 * 飛び駒の利きを評価します.
 *
 * （参考文献）
 *   - 金澤裕治, NineDayFeverアピール文書,
 *     http://www.computer-shogi.org/wcsc25/appeal/NineDayFever/NDF-2015.txt, 2015.
 */
template<Color kColor>
FORCE_INLINE PackedScore EvaluateSlidingPieces(const Position& pos) {
  PackedScore sum(0);

  Square own_ksq = pos.king_square(kColor);
  Square opp_ksq = pos.king_square(~kColor);
  if (kColor == kWhite) {
    own_ksq = Square::rotate180(own_ksq);
    opp_ksq = Square::rotate180(opp_ksq);
  }

  // 飛車の利きを評価
  pos.pieces(kColor, kRook, kDragon).ForEach([&](Square from) {
    Bitboard rook_target = pos.pieces() | ~rook_mask_bb(from);
    Bitboard attacks = rook_attacks_bb(from, pos.pieces()) & rook_target;
    assert(2 <= attacks.count() && attacks.count() <= 4);
    if (kColor == kWhite) {
      from = Square::rotate180(from);
    }
    attacks.ForEach([&](Square to) {
      Piece threatened = pos.piece_on(to);
      if (kColor == kWhite) {
        to = Square::rotate180(to);
        if (threatened != kNoPiece) threatened = threatened.opponent_piece();
      }
      sum += g_eval_params->rook_control[kBlack][own_ksq][from][to];
      sum += g_eval_params->rook_control[kWhite][opp_ksq][from][to];
      sum += g_eval_params->rook_threat[opp_ksq][to][threatened];
    });
  });

  // 角の利きを評価
  Bitboard edge = file_bb(kFile1) | file_bb(kFile9) | rank_bb(kRank1) | rank_bb(kRank9);
  Bitboard bishop_target = pos.pieces() | edge;
  pos.pieces(kColor, kBishop, kHorse).ForEach([&](Square from) {
    Bitboard attacks = bishop_attacks_bb(from, pos.pieces()) & bishop_target;
    assert(1 <= attacks.count() && attacks.count() <= 4);
    if (kColor == kWhite) {
      from = Square::rotate180(from);
    }
    attacks.ForEach([&](Square to) {
      Piece threatened = pos.piece_on(to);
      if (kColor == kWhite) {
        to = Square::rotate180(to);
        if (threatened != kNoPiece) threatened = threatened.opponent_piece();
      }
      sum += g_eval_params->bishop_control[kBlack][own_ksq][from][to];
      sum += g_eval_params->bishop_control[kWhite][opp_ksq][from][to];
      sum += g_eval_params->bishop_threat[opp_ksq][to][threatened];
    });
  });

  // 香車の利きを評価
  Bitboard lance_target = pos.pieces() | rank_bb(relative_rank(kColor, kRank1));
  pos.pieces(kColor, kLance).ForEach([&](Square from) {
    Bitboard attacks = lance_attacks_bb(from, pos.pieces(), kColor) & lance_target;
    if (attacks.any()) {
      assert(attacks.count() == 1);
      Square to = attacks.first_one();
      Piece threatened = pos.piece_on(to);
      if (kColor == kWhite) {
        from = Square::rotate180(from);
        to = Square::rotate180(to);
        if (threatened != kNoPiece) threatened = threatened.opponent_piece();
      }
      sum += g_eval_params->lance_control[kBlack][own_ksq][from][to];
      sum += g_eval_params->lance_control[kWhite][opp_ksq][from][to];
      sum += g_eval_params->lance_threat[opp_ksq][to][threatened];
    }
  });

  return kColor == kBlack ? sum : FlipScores2x2(sum);
}

PackedScore EvaluateSlidingPieces(const Position& pos) {
  PackedScore score_black = EvaluateSlidingPieces<kBlack>(pos);
  PackedScore score_white = EvaluateSlidingPieces<kWhite>(pos);
  return score_black + score_white;
}

/**
 * 評価関数の差分計算を行います（玉の移動手の場合）.
 */
EvalDetail EvaluateDifferenceForKingMove(const Position& pos,
                                         const EvalDetail& previous_eval,
                                         PsqList* const list) {
  assert(list != nullptr);
  assert(pos.last_move().piece_type() == kKing);

  EvalDetail diff;

  const Move move = pos.last_move();
  const Piece piece = move.piece();
  const Square to = move.to();
  const Color king_color = piece.color();

  // 1. 駒を取った場合の計算
  if (move.is_capture()) {
    Piece captured = move.captured_piece();
    // a. KP・PPスコアから古い特徴を除外する
    PsqPair old_psq = PsqPair::OfBoard(captured, to);
    diff -= SumPositionalScore(old_psq, *list, pos);
    // b. インデックスリストを更新する
    list->MakeMove(move);
    // c. KP・PPスコアに新しい特徴を追加する
    PieceType hand_type = captured.hand_type();
    int num = pos.hand(king_color).count(hand_type);
    PsqPair new_psq = PsqPair::OfHand(king_color, hand_type, num);
    diff += SumPositionalScore(new_psq, *list, pos);
  }

  // 2. 移動した玉に関するKPスコアを再計算する
  PackedScore sum_of_kp(0);
  if (king_color == kBlack) {
    Square king_square = to;
    for (const PsqPair& i : *list) {
      sum_of_kp += g_eval_params->king_piece[king_square][i.black()];
    }
    diff.kp[kBlack] = sum_of_kp - previous_eval.kp[kBlack];
  } else {
    Square king_square = Square::rotate180(to);
    for (const PsqPair& i : *list) {
      sum_of_kp += g_eval_params->king_piece[king_square][i.white()];
    }
    diff.kp[kWhite] = FlipScores3x1(sum_of_kp) - previous_eval.kp[kWhite];
  }


  // 3. 移動した玉に関するやねうら王classicのKK、KKP、KPPのスコアを再計算する
  YaneuraOuClassicEvalDetail yaneuraou_classic_eval_detail;

  const Square sq_bk = pos.king_square(kBlack);
  const Square inv_sq_wk = Square::rotate180(pos.king_square(kWhite));

  // KK
  yaneuraou_classic_eval_detail.kk_board = kk_yaneuraou_classic[sq_bk][inv_sq_wk];

  diff.yaneuraou_classic_eval_detail.kk_board = yaneuraou_classic_eval_detail.kk_board - previous_eval.yaneuraou_classic_eval_detail.kk_board;


  // KKP
  for (const PsqPair* i = list->begin(); i != list->end(); ++i) {
    yaneuraou_classic_eval_detail.kkp_board += kkp_yaneuraou_classic[sq_bk][inv_sq_wk][i->black()];
  }

  diff.yaneuraou_classic_eval_detail.kkp_board = yaneuraou_classic_eval_detail.kkp_board - previous_eval.yaneuraou_classic_eval_detail.kkp_board;


  // KPP
  if (king_color == kBlack) {
    for (const PsqPair* i = list->begin(); i != list->end(); ++i) {
      for (const PsqPair* j = list->begin(); j < i; ++j) {
        yaneuraou_classic_eval_detail.kpp_board[kBlack] += kpp_yaneuraou_classic[sq_bk][i->black()][j->black()];
      }
    }

    diff.yaneuraou_classic_eval_detail.kpp_board[kBlack] = yaneuraou_classic_eval_detail.kpp_board[kBlack] - previous_eval.yaneuraou_classic_eval_detail.kpp_board[kBlack];

  } else {
    for (const PsqPair* i = list->begin(); i != list->end(); ++i) {
      for (const PsqPair* j = list->begin(); j < i; ++j) {
        yaneuraou_classic_eval_detail.kpp_board[kWhite] += kpp_yaneuraou_classic[inv_sq_wk][i->white()][j->white()];
      }
    }

    diff.yaneuraou_classic_eval_detail.kpp_board[kWhite] = yaneuraou_classic_eval_detail.kpp_board[kWhite] - previous_eval.yaneuraou_classic_eval_detail.kpp_board[kWhite];
  }

  return diff;
}

/**
 * 評価関数の差分計算を行います（玉の移動手以外の手を指した場合）.
 */
EvalDetail EvaluateDifferenceForNonKingMove(const Position& pos,
                                            PsqList* const list) {
  assert(list != nullptr);
  assert(pos.last_move().piece_type() != kKing);

  EvalDetail diff;

  const Move move = pos.last_move();
  const Piece piece = move.piece();
  const Square to = move.to();
  const Color side_to_move = piece.color();

  if (move.is_drop()) {
    PieceType pt = piece.type();
    // 1. 古い特徴を除外する
    int num = pos.hand(side_to_move).count(pt) + 1;
    PsqPair old_psq = PsqPair::OfHand(side_to_move, pt, num);
    diff -= SumPositionalScore(old_psq, *list, pos);
    // 2. インデックスリストを更新する
    list->MakeMove(move);
    // 3. 新しい特徴を追加する
    PsqPair new_psq = PsqPair::OfBoard(piece, to);
    diff += SumPositionalScore(new_psq, *list, pos);
  } else if (move.is_capture()) {
    Piece captured = move.captured_piece();
    Square from = move.from();
    // 1. 古い特徴を除外する
    PsqPair old_psq1 = PsqPair::OfBoard(piece, from);
    PsqPair old_psq2 = PsqPair::OfBoard(captured, to);
    diff -= SumPositionalScore(old_psq1, old_psq2, *list, pos);
    // 2. インデックスリストを更新する
    list->MakeMove(move);
    // 3. 新しい特徴を追加する
    PieceType hand_type = captured.hand_type();
    int num = pos.hand(side_to_move).count(hand_type);
    PsqPair new_psq1 = PsqPair::OfBoard(move.piece_after_move(), to);
    PsqPair new_psq2 = PsqPair::OfHand(side_to_move, hand_type, num);
    diff += SumPositionalScore(new_psq1, new_psq2, *list, pos);
  } else {
    Square from = move.from();
    // 1. 古い特徴を除外する
    PsqPair old_psq = PsqPair::OfBoard(piece, from);
    diff -= SumPositionalScore(old_psq, *list, pos);
    // 2. インデックスリストを更新する
    list->MakeMove(move);
    // 3. 新しい特徴を増加する
    PsqPair new_psq = PsqPair::OfBoard(move.piece_after_move(), to);
    diff += SumPositionalScore(new_psq, *list, pos);
  }

  return diff;
}

} // namespace

Score Evaluation::Evaluate(const Position& pos) {
  if (!pos.king_exists(kBlack) || !pos.king_exists(kWhite)) {
    return kScoreZero;
  } else {
    PsqList psq_list(pos);
    return EvaluateAll(pos, psq_list).ComputeFinalScore(pos.side_to_move());
  }
}

EvalDetail Evaluation::EvaluateAll(const Position& pos,
                                    const PsqList& psq_list) {
  EvalDetail sum;

  if (!pos.king_exists(kBlack) || !pos.king_exists(kWhite)) {
    return sum;
  }

  // 1. 駒の位置評価
  sum += EvaluatePositionalAdvantage(pos, psq_list);

  // 2. 各マスの利き
  PsqControlList psq_control_list = pos.extended_board().GetPsqControlList();
  sum.controls = EvaluateControls(pos, psq_control_list);

  // 3. 玉の安全度
  sum.king_safety = EvaluateKingSafety(pos);

  // 4. 飛車・角・香車の利き
  sum.sliders = EvaluateSlidingPieces(pos);

  // 5. やねうら王classicの評価値（全計算）
  sum.yaneuraou_classic_eval_detail = YaneuraOuClassicEval::ComputeEval(pos, psq_list);

  return sum;
}

EvalDetail Evaluation::EvaluateDifference(const Position& pos,
                                          const EvalDetail& previous_eval,
                                          const PsqControlList& previous_list,
                                          const PsqControlList& current_list,
                                          PsqList* const psq_list) {
  assert(psq_list != nullptr);
  assert(pos.last_move().is_real_move());

  EvalDetail diff;

  if (!pos.king_exists(kBlack) || !pos.king_exists(kWhite)) {
    psq_list->MakeMove(pos.last_move()); // インデックスリストの更新のみ行う
    return diff;
  }

#ifndef NDEBUG
  // PsqListの更新はEvaluateDifferenceImpl()において評価値の差分計算と同時に行われるため、
  // 事前にPsqListの更新をしてはならない
  //（但し、駒を取らずに玉を動かす手の場合は、インデックスリストの更新がそもそも必要ないのでチェック不要）
  if (!pos.last_move().piece().is(kKing) || pos.last_move().is_capture()) {
    assert(!PsqList::TwoListsHaveSameItems(*psq_list, PsqList(pos)));
  }
#endif

  // 1. 駒の位置評価と、各マスの利き評価（差分計算）
  if (pos.last_move().piece().is(kKing)) {
    // a. 駒の位置評価
    diff = EvaluateDifferenceForKingMove(pos, previous_eval, psq_list);
    // b. 各マスの利き評価（ここについては、再計算）
    diff.controls = EvaluateControls(pos, current_list) - previous_eval.controls;
  } else {
    // a. 駒の位置評価
    diff = EvaluateDifferenceForNonKingMove(pos, psq_list);
    // b. 各マスの利き評価
    diff.controls = EvaluateDifferenceOfControls(pos, previous_list, current_list);
  }
  // 差分計算の途中で、PsqListの差分計算が正しく行われたかをチェック
  assert(PsqList::TwoListsHaveSameItems(*psq_list, PsqList(pos)));

  // 2. 玉の安全度（末端評価）
  diff.king_safety = EvaluateKingSafety(pos) - previous_eval.king_safety;

  // 3. 飛車・角・香車の利き（末端評価）
  diff.sliders = EvaluateSlidingPieces(pos) - previous_eval.sliders;

  // 4. やねうら王classicの駒割りの差分計算
  diff.yaneuraou_classic_eval_detail.material = YaneuraOuClassicEval::EvaluateDifferenceOfMaterial(pos) * YaneuraOuClassicEval::FV_SCALE;

  return diff;
}

void Evaluation::Init() {
  ReadParametersFromFile("params.bin");
}

void Evaluation::ReadParametersFromFile(const char* file_name) {
  // Read parameters from file.
  std::FILE* fp = std::fopen(file_name, "rb");
  if (fp == nullptr) {
    std::printf("info string Failed to open %s.\n", file_name);
    return;
  }
  if (std::fread(g_eval_params.get(), sizeof(EvalParameters), 1, fp) != 1) {
    std::printf("info string Failed to read %s.\n", file_name);
    std::fclose(fp);
    return;
  }
  std::fclose(fp);
}


/**
 * やねうら王classicの評価値関連
 */
namespace YaneuraOuClassicEval {

  /** 駒の価値 */
  const int PieceValue[32] = {
      0        , PawnValue   , LanceValue   , KnightValue   , SilverValue   , GoldValue, BishopValue, RookValue
    , KingValue, ProPawnValue, ProLanceValue, ProKnightValue, ProSilverValue, 0        , HorseValue , DragonValue
    , 0         , -PawnValue   , -LanceValue   , -KnightValue   , -SilverValue   , -GoldValue, -BishopValue, -RookValue
    , -KingValue, -ProPawnValue, -ProLanceValue, -ProKnightValue, -ProSilverValue, 0         , -HorseValue , -DragonValue
  };

  /** 駒を取る価値 */
  const int CapturePieceValue[32] = {
      0, PawnValue * 2, LanceValue * 2, KnightValue * 2, SilverValue * 2
    , GoldValue * 2, BishopValue * 2, RookValue * 2
    , 0, ProPawnValue + PawnValue, ProLanceValue + LanceValue, ProKnightValue + KnightValue, ProSilverValue + SilverValue
    , 0, HorseValue + BishopValue, DragonValue + RookValue
    , 0, PawnValue * 2, LanceValue * 2, KnightValue * 2, SilverValue * 2
    , GoldValue * 2, BishopValue * 2, RookValue * 2
    , 0, ProPawnValue + PawnValue, ProLanceValue + LanceValue, ProKnightValue + KnightValue, ProSilverValue + SilverValue
    , 0, HorseValue + BishopValue, DragonValue + RookValue
  };

  /** 駒が成る価値 */
  const int ProDiffPieceValue[32] = {
    0, ProPawnValue - PawnValue, ProLanceValue - LanceValue, ProKnightValue - KnightValue, ProSilverValue - SilverValue, 0, HorseValue - BishopValue, DragonValue - RookValue ,
    0, ProPawnValue - PawnValue, ProLanceValue - LanceValue, ProKnightValue - KnightValue, ProSilverValue - SilverValue, 0, HorseValue - BishopValue, DragonValue - RookValue ,
    0, ProPawnValue - PawnValue, ProLanceValue - LanceValue, ProKnightValue - KnightValue, ProSilverValue - SilverValue, 0, HorseValue - BishopValue, DragonValue - RookValue ,
    0, ProPawnValue - PawnValue, ProLanceValue - LanceValue, ProKnightValue - KnightValue, ProSilverValue - SilverValue, 0, HorseValue - BishopValue, DragonValue - RookValue
  };

} // namespace


// やねうら王classicの評価関数ファイルの読込み
void YaneuraOuClassicEval::LoadEval() {

  //----- やねうら王classicの評価関数ファイルの読込み（KK、KKP）
  // ・tmp_kkp1へ読み込む
  std::string filename_kkp = g_YaneuraOuClassicEvalFolder + "/kkp32ap.bin";
  std::fstream fs_kkp;
  fs_kkp.open(filename_kkp, std::ios::in | std::ios::binary);
  if (fs_kkp.fail()) {
    std::printf("info string Failed to open %s.\n", filename_kkp.c_str());
    exit(EXIT_FAILURE);
  }

  // 評価関数ファイルの読込み用
  int count_kkp = SQ_NB_PLUS1 * SQ_NB_PLUS1 * (fe_end + 1);
  size_t size_kkp = count_kkp * (int)sizeof(ValueKkp);
  ValueKkp* tmp_kkp1 = new ValueKkp[size_kkp];

  fs_kkp.read((char*)tmp_kkp1, size_kkp);
  if (fs_kkp.fail()) {
    std::printf("info string Failed to read %s.\n", filename_kkp.c_str());
    delete[] tmp_kkp1;
    exit(EXIT_FAILURE);
  }
  fs_kkp.close();


  //----- やねうら王classicの評価関数ファイルの読込み（KPP）
  // ・tmp_kpp1へ読み込む
  std::string filename_kpp = g_YaneuraOuClassicEvalFolder + "/kpp16ap.bin";
  std::fstream fs_kpp;
  fs_kpp.open(filename_kpp, std::ios::in | std::ios::binary);
  if (fs_kpp.fail()) {
    std::printf("info string Failed to open %s.\n", filename_kpp.c_str());
    exit(EXIT_FAILURE);
  }

  // 評価関数ファイルの読込み用
  int count_kpp = SQ_NB_PLUS1 * fe_end * fe_end;
  size_t size_kpp = count_kpp * (int)sizeof(ValueKpp);
  ValueKpp* tmp_kpp1 = new ValueKpp[size_kpp];

  fs_kpp.read((char*)tmp_kpp1, size_kpp);
  if (fs_kpp.fail()) {
    std::printf("info string Failed to read %s.\n", filename_kpp.c_str());
    delete[] tmp_kkp1;
    delete[] tmp_kpp1;
    exit(EXIT_FAILURE);
  }
  fs_kpp.close();


  //----- 持ち駒の添字のコンバート時の間違い対応
  // ・「tmp_kkp1、tmp_kpp1」から「tmp_kkp2、tmp_kpp2」へ

  #define TMP_KKP1(k1, k2, p) tmp_kkp1[k1 * SQ_NB_PLUS1 * (fe_end + 1) + k2 * (fe_end + 1) + p]
  #define TMP_KPP1(k, p1, p2) tmp_kpp1[k * fe_end * fe_end + p1 * fe_end + p2]

  #define TMP_KKP2(k1, k2, p) tmp_kkp2[k1 * SQ_NB_PLUS1 * (fe_end + 1) + k2 * (fe_end + 1) + p]
  #define TMP_KPP2(k, p1, p2) tmp_kpp2[k * fe_end * fe_end + p1 * fe_end + p2]

  ValueKkp* tmp_kkp2 = new ValueKkp[count_kkp];
  ValueKpp* tmp_kpp2 = new ValueKpp[count_kpp];

  memset(tmp_kkp2, 0, size_kkp);
  memset(tmp_kpp2, 0, size_kpp);

  // KKP
  for (int k1 = 0; k1 < SQ_NB_PLUS1; ++k1) {
    for (int k2 = 0; k2 < SQ_NB_PLUS1; ++k2) {
      for (int j = 1; j < fe_end + 1; ++j) {
        int j2 = j < fe_hand_end ? j - 1 : j;
        TMP_KKP2(k1, k2, j) = TMP_KKP1(k1, k2, j2);
      }
    }
  }

  // KPP
  for (int k = 0; k < SQ_NB_PLUS1; ++k) {
    for (int i = 1; i < fe_end; ++i) {
      for (int j = 1; j < fe_end; ++j) {
        int i2 = i < fe_hand_end ? i - 1 : i;
        int j2 = j < fe_hand_end ? j - 1 : j;
        TMP_KPP2(k, i, j) = TMP_KPP1(k, i2, j2);
      }
    }
  }

  // メモリ解放
  delete[] tmp_kpp1;
  delete[] tmp_kkp1;

  // undef
  #undef TMP_KKP1
  #undef TMP_KPP1


  //----- やねうら王classicの評価関数パラメータ（技巧のPsqIndexへの変換後）へ格納
  // ・「tmp_kkp2、tmp_kpp2」から「kk_yaneuraou_classic、kkp_yaneuraou_classic、kpp_yaneuraou_classic」へ

  // KK
  for (int k1 = 0; k1 < 81; ++k1) {
    for (int k2 = 0; k2 < 81; ++k2) {
      kk_yaneuraou_classic[k1][k2] = TMP_KKP2(k1, k2, fe_end);
    }
  }

  // KKP
  for (int k1 = 0; k1 < 81; ++k1) {
    for (int k2 = 0; k2 < 81; ++k2) {
      for (int p = 0; p < 2110; ++p) {
        kkp_yaneuraou_classic[k1][k2][p] = TMP_KKP2(k1, k2, GetYaneuraOuClassicPsqIndex((PsqIndex)p));
      }
    }
  }

  // KPP
  for (int k = 0; k < 81; ++k) {
    for (int p1 = 0; p1 < 2110; ++p1) {
      for (int p2 = 0; p2 < 2110; ++p2) {
        kpp_yaneuraou_classic[k][p1][p2] = TMP_KPP2(k, GetYaneuraOuClassicPsqIndex((PsqIndex)p1), GetYaneuraOuClassicPsqIndex((PsqIndex)p2));
      }
    }
  }

  //-----

  // メモリ解放
  delete[] tmp_kkp2;
  delete[] tmp_kpp2;

  // undef
  #undef TMP_KKP2
  #undef TMP_KPP2
}

// やねうら王classicの評価値の全計算
YaneuraOuClassicEvalDetail YaneuraOuClassicEval::ComputeEval(const Position& pos, const PsqList& list) {
  YaneuraOuClassicEvalDetail yaneuraou_classic_eval_detail;

  // 駒割り
  yaneuraou_classic_eval_detail.material = EvaluateMaterial(pos) * FV_SCALE;

  const Square sq_bk = pos.king_square(kBlack);
  //const Square sq_wk = pos.king_square(kWhite);
  const Square inv_sq_wk = Square::rotate180(pos.king_square(kWhite));

  // KK
  yaneuraou_classic_eval_detail.kk_board = kk_yaneuraou_classic[sq_bk][inv_sq_wk];

  for (const PsqPair* i = list.begin(); i != list.end(); ++i) {
    // KKP
    yaneuraou_classic_eval_detail.kkp_board += kkp_yaneuraou_classic[sq_bk][inv_sq_wk][i->black()];

    for (const PsqPair* j = list.begin(); j < i; ++j) {
      // KPP
      yaneuraou_classic_eval_detail.kpp_board[kBlack] += kpp_yaneuraou_classic[sq_bk][i->black()][j->black()];
      yaneuraou_classic_eval_detail.kpp_board[kWhite] += kpp_yaneuraou_classic[inv_sq_wk][i->white()][j->white()];
    }
  }

  return yaneuraou_classic_eval_detail;
}

// やねうら王classicの駒割りの全計算
Score YaneuraOuClassicEval::EvaluateMaterial(const Position& pos) {
  Score score = kScoreZero;

  // 盤上の駒
  for (const Square sq : Square::all_squares()) {
    score += PieceValue[pos.piece_on(sq)];
  }

  // 持ち駒
  for (Color c : { kBlack, kWhite }) {
    for (PieceType pt : Piece::all_hand_types()) {
      score += (c == kBlack ? 1 : -1) * pos.hand(c).count(pt) * PieceValue[pt];
    }
  }

  return score;
}

// やねうら王classicの駒割りの差分計算
Score YaneuraOuClassicEval::EvaluateDifferenceOfMaterial(const Position& pos) {
  const Move move = pos.last_move();

  // 駒打ち
  if (move.is_drop()) {
    return kScoreZero;
  }

  // 駒割りの差分計算用
  Score materialDiff = kScoreZero;

  // 成る指し手
  if (move.is_promotion()) {
    materialDiff +=  ProDiffPieceValue[move.piece()];
  }

  // 駒を取る指し手
  if (move.is_capture()) {
    materialDiff += CapturePieceValue[move.captured_piece()];
  }

  // 通常と符号が逆なので注意（先手の場合、マイナス）
  return (pos.side_to_move() == kBlack ? -materialDiff : materialDiff);
}

// やねうら王classicの評価値の計算
int32_t YaneuraOuClassicEvalDetail::Sum(const Color side_to_move) const {
  // 駒の位置（kpp_board[kWhite]のみマイナス）
  // ・KKとKKPはFV_SCALE_KK_KKPで割る
  int32_t score_board = (kk_board + kkp_board) / YaneuraOuClassicEval::FV_SCALE_KK_KKP + (kpp_board[kBlack] - kpp_board[kWhite]);

  // 手番
  // ・存在しない

  // 合計
  int32_t score_sum = (side_to_move == kBlack ? (material + score_board) : -(material + score_board));

  return score_sum;
}

// やねうら王classicの生の評価値を１歩＝１００点（centipawn）の評価値に変換する
double YaneuraOuClassicEval::ToCentiPawn(const int32_t value) {
  return ((double)value) / FV_SCALE * 100 / (int)PawnValue;
}

// やねうら王classicの生の評価値を１歩＝１００点（centipawn）の評価値（手番側から見た評価値）に変換する
double YaneuraOuClassicEval::ToCentiPawn(const int32_t value, const Color side_to_move) {
  return side_to_move == kBlack ? ToCentiPawn(value) : -ToCentiPawn(value);
}

// 評価値の詳細情報を標準出力へ出力する
void Evaluation::Print(const Position& pos) {
  PsqList psq_list(pos);
  EvaluateAll(pos, psq_list).Print(pos.side_to_move());
}

/**
 * 技巧の生の評価値を１歩＝１００点（centipawn）の評価値（手番側から見た評価値）に変換します.
 * @param value 生の評価値
 * @param side_to_move 手番
 * @return １歩＝１００点（centipawn）の評価値（手番側から見た評価値）
 */
double GikouEvalToCentiPawn(const int64_t value, Color side_to_move) {
  constexpr int64_t kScale = Progress::kWeightScale;
  double score = ((double)value) / (kScale * static_cast<int64_t>(kFvScale));
  return side_to_move == kBlack ? score : -score;
}

/**
 * KPの評価値を計算します.
 * @param kp_total KPのPackedScoreの合計
 * @param progress 進行度
 * @param side_to_move 手番
 * @return KPの評価値（手番側から見た評価値）
 */
double ComputeEvalKp(PackedScore kp_total, int64_t progress, Color side_to_move) {
  constexpr int64_t kScale = Progress::kWeightScale;

  int64_t sum = 0;
  int64_t tempo = 0;

  if (progress < (kScale / 2)) {
    int64_t opening     = -2 * progress + 1 * kScale;
    int64_t middle_game = +2 * progress             ;
    sum += (opening * kp_total[0]) + (middle_game * kp_total[1]);
    tempo = (opening * g_eval_params->tempo[0]) + (middle_game * g_eval_params->tempo[1]);
  } else {
    int64_t middle_game = -2 * progress + 2 * kScale;
    int64_t end_game    = +2 * progress - 1 * kScale;
    sum += (middle_game * kp_total[1]) + (end_game * kp_total[2]);
    tempo = (middle_game * g_eval_params->tempo[1]) + (end_game * g_eval_params->tempo[2]);
  }

  if (side_to_move == kBlack) {
    sum += tempo / 2;
  } else {
    sum -= tempo / 2;
  }

  return GikouEvalToCentiPawn(sum, side_to_move);
}

/**
 * KP以外の評価値を計算します.
 * @param others KP以外のPackedScore
 * @param progress 進行度
 * @param side_to_move 手番
 * @return KP以外の評価値（手番側から見た評価値）
 */
double ComputeEvalOthers(PackedScore others, int64_t progress, Color side_to_move) {
  constexpr int64_t kScale = Progress::kWeightScale;
  int64_t sum = 0;

  if (side_to_move == kBlack) {
    int64_t opening  = (kScale - progress) * (others[0] + others[1] / 10);
    int64_t end_game = (progress         ) * (others[2] + others[3] / 10);
    sum += (opening + end_game);
  } else {
    int64_t opening  = (kScale - progress) * (others[0] - others[1] / 10);
    int64_t end_game = (progress         ) * (others[2] - others[3] / 10);
    sum += (opening + end_game);
  }

  return GikouEvalToCentiPawn(sum, side_to_move);
}

// 評価値の詳細情報を標準出力へ出力する
void EvalDetail::Print(Color side_to_move) const {
  // 最終的な評価値
  Score final_score = ComputeFinalScore(side_to_move);

  // 以下、評価値の内訳も出力したいので、部分的にComputeFinalScoreと似た計算を行う

  // KPの合計
  PackedScore kp_total = kp[kBlack] + kp[kWhite];

  // 進行度を求める
  constexpr int64_t kScale = Progress::kWeightScale;
  double weight = static_cast<double>(kp_total[3]);
  double progress_double = math::sigmoid(weight * double(1.0 / kScale));
  int64_t progress = static_cast<int64_t>(progress_double * kScale);

  // 技巧の評価値を要素ごとに算出する
  double score_kp_total    = ComputeEvalKp    (kp_total   , progress, side_to_move);
  double score_controls    = ComputeEvalOthers(controls   , progress, side_to_move);
  double score_two_pieces  = ComputeEvalOthers(two_pieces , progress, side_to_move);
  double score_king_safety = ComputeEvalOthers(king_safety, progress, side_to_move);
  double score_sliders     = ComputeEvalOthers(sliders    , progress, side_to_move);

  // 技巧の評価値（double）
  double score_gikou = score_kp_total + score_controls + score_two_pieces + score_king_safety + score_sliders;

  // やねうら王classicの評価値（double）
  double score_yaneuraou_classic = YaneuraOuClassicEval::ToCentiPawn(yaneuraou_classic_eval_detail.Sum(side_to_move));

  // 序盤・中盤・終盤の評価値の割合
  double rate_opening_yaneuraou_classic = (double)g_YaneuraOuClassicEvalOpening / 100;
  double rate_opening_gikou = 1 - rate_opening_yaneuraou_classic;

  double rate_middle_game_yaneuraou_classic = (double)g_YaneuraOuClassicEvalMiddleGame / 100;
  double rate_middle_game_gikou = 1 - rate_middle_game_yaneuraou_classic;

  double rate_end_game_yaneuraou_classic = (double)g_YaneuraOuClassicEvalEndGame / 100;
  double rate_end_game_gikou = 1 - rate_end_game_yaneuraou_classic;

  // 現在の進行度での技巧、やねうら王classicの評価値の割合
  double rate_gikou = 0;
  double rate_yaneuraou_classic = 0;

  if (progress_double < 0.5) {
    rate_gikou = (1 - progress_double * 2) * rate_opening_gikou + (progress_double * 2) * rate_middle_game_gikou;
    rate_yaneuraou_classic = (1 - progress_double * 2) * rate_opening_yaneuraou_classic + (progress_double * 2) * rate_middle_game_yaneuraou_classic;
  } else {
    rate_gikou = (2 - progress_double * 2) * rate_middle_game_gikou + (progress_double * 2 - 1) * rate_end_game_gikou;
    rate_yaneuraou_classic = (2 - progress_double * 2) * rate_middle_game_yaneuraou_classic + (progress_double * 2 - 1) * rate_end_game_yaneuraou_classic;
  }

  // 最終的な評価値
  int score_mix = score_gikou * rate_gikou + score_yaneuraou_classic * rate_yaneuraou_classic;
  score_mix = std::max(std::min(score_mix, (int)(kScoreMaxEval - 1)), (int)(- kScoreMaxEval + 1));


// デバッグ用
#if 0
  if (score_mix != final_score) {
    printf("score_mix != final_score\n");
    printf("score_mix  =%d\n", score_mix);
    printf("final_score=%d\n", final_score);
  }
#endif


  // 最終的な評価値の情報を標準出力へ出力する
  std::printf("---------- Eval\n");
  std::printf("Eval        =%+6d\n", final_score);
  std::printf("-----\n");
  std::printf("Gikou       =%+9.2f\n", score_gikou);
  std::printf("YaneuraOu   =%+9.2f\n", YaneuraOuClassicEval::ToCentiPawn(yaneuraou_classic_eval_detail.Sum(side_to_move)));
  std::printf("-----\n");
  std::printf("SideToMove  = %s\n", side_to_move == kBlack ? "Black(Sente)" : "White(Gote)");
  std::printf("Progress(%%) =%9.2f%%\n", progress_double * 100);
  std::printf("Gikou(%%)    =%9.2f%%\n", rate_gikou * 100);
  std::printf("YaneuraOu(%%)=%9.2f%%\n", rate_yaneuraou_classic * 100);


  // 技巧の評価値の情報を標準出力へ出力する
  std::printf("---------- Gikou\n");
  std::printf("Sum         =%+9.2f\n", score_gikou);
  std::printf("-----\n");
  std::printf("KP          =%+9.2f\n", score_kp_total);
  std::printf("PP          =%+9.2f\n", score_two_pieces);
  std::printf("Controls    =%+9.2f\n", score_controls);
  std::printf("KingSafety  =%+9.2f\n", score_king_safety);
  std::printf("Sliders     =%+9.2f\n", score_sliders);


  // やねうら王classicの評価値の情報を標準出力へ出力する
  yaneuraou_classic_eval_detail.Print(side_to_move);
}

// やねうら王classicの評価値の情報を標準出力へ出力する
void YaneuraOuClassicEvalDetail::Print(const Color side_to_move) const {
  std::printf("---------- YaneuraOuClassic\n");
  std::printf("Sum         =%+9.2f\n", YaneuraOuClassicEval::ToCentiPawn(Sum(side_to_move)));
  std::printf("-----\n");
  std::printf("Material    =%+9.2f\n", YaneuraOuClassicEval::ToCentiPawn(material, side_to_move));
  std::printf("KK          =%+9.2f\n", YaneuraOuClassicEval::ToCentiPawn((double)(kk_board)  / YaneuraOuClassicEval::FV_SCALE_KK_KKP, side_to_move));
  std::printf("KKP         =%+9.2f\n", YaneuraOuClassicEval::ToCentiPawn((double)(kkp_board) / YaneuraOuClassicEval::FV_SCALE_KK_KKP, side_to_move));
  std::printf("KPP         =%+9.2f\n", YaneuraOuClassicEval::ToCentiPawn(kpp_board[kBlack] - kpp_board[kWhite], side_to_move));
  std::printf("----------\n");
}

