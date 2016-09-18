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

#ifndef EVALUATION_H_
#define EVALUATION_H_

#include <memory>
#include "common/bitset.h"
#include "common/pack.h"
#include "common/valarraykey.h"
#include "hand.h"
#include "piece.h"
#include "psq.h"
#include "square.h"
#include "types.h"
class Position;

/**
 * 評価値のスケールです.
 * 生の評価値をこのスケールで割ることで、１歩＝１００点（centipawn）の評価値を得ることができます。
 * 言い換えると、現在の実装では、評価値の計算に固定小数点を使っていることになります。
 */
constexpr int32_t kFvScale = 1 << 16;

/**
 * 4個の32ビット符合付整数をひとつにまとめたものです.
 *
 * Packクラスは、内部的にSIMD演算を利用しているので、４個の整数値を同時に計算することができます。
 * ４個の整数値は、以下のように、進行度ごとに異なった評価パラメータを格納しています。
 *
 * 1. KPの場合
 *   - [0] 序盤
 *   - [1] 中盤
 *   - [2] 終盤
 *   - [3] 進行度計算用の重み（評価値ではありませんが、進行度の計算を評価値の計算と同時に行うため、ここに格納してあります）
 *
 * 2. KP以外の評価項目の場合
 *   - [0] 序盤
 *   - [1] 手番（序盤用）
 *   - [2] 終盤
 *   - [3] 手番（終盤用）
 *
 * （参考文献）
 *   - 金子知適: 「GPS将棋」の評価関数とコンピュータ将棋による棋譜の検討,
 *     『コンピュータ将棋の進歩６』, pp.25-45, 共立出版, 2012.
 *   - 金澤裕治: NineDayFeverアピール文書,
 *     http://www.computer-shogi.org/wcsc24/appeal/NineDayFever/NDF.txt, 2014.
 *   - 山下宏: YSSアピール文書,
 *     http://www.computer-shogi.org/wcsc25/appeal/YSS/yssappeal2015_2.txt, 2015.
 */
typedef Pack<int32_t, 4> PackedScore;

/**
 * Aperyの評価値の詳細を保存するためのクラスです.
 * ・試験的に手番の評価をなくしたバージョン。KPPTではなくKPP。
 * ・評価関数の精度は下がるが、npsは向上する。
 */
struct AperyEvalDetail {

  AperyEvalDetail() {
    Clear();
  }

  void Clear() {
    std::memset(this, 0, sizeof(*this));
  }

  AperyEvalDetail operator+(const AperyEvalDetail& rhs) const {
    return AperyEvalDetail(*this) += rhs;
  }

  AperyEvalDetail operator-(const AperyEvalDetail& rhs) const {
    return AperyEvalDetail(*this) -= rhs;
  }

  AperyEvalDetail& operator+=(const AperyEvalDetail& rhs) {
    material += rhs.material;
    kk_board += rhs.kk_board;
    //kk_turn += rhs.kk_turn;
    kkp_board += rhs.kkp_board;
    //kkp_turn += rhs.kkp_turn;
    kpp_board[kBlack] += rhs.kpp_board[kBlack];
    //kpp_turn [kBlack] += rhs.kpp_turn [kBlack];
    kpp_board[kWhite] += rhs.kpp_board[kWhite];
    //kpp_turn [kWhite] += rhs.kpp_turn [kWhite];

    return *this;
  }

  AperyEvalDetail& operator-=(const AperyEvalDetail& rhs) {
    material -= rhs.material;
    kk_board -= rhs.kk_board;
    //kk_turn -= rhs.kk_turn;
    kkp_board -= rhs.kkp_board;
    //kkp_turn -= rhs.kkp_turn;
    kpp_board[kBlack] -= rhs.kpp_board[kBlack];
    //kpp_turn [kBlack] -= rhs.kpp_turn [kBlack];
    kpp_board[kWhite] -= rhs.kpp_board[kWhite];
    //kpp_turn [kWhite] -= rhs.kpp_turn [kWhite];

    return *this;
  }

  /**
   * Aperyの評価値を計算します.
   * @param side_to_move 手番
   * @return Aperyの評価値（手番側から見た評価値）
   */
  int32_t Sum(const Color side_to_move) const;

  /**
   * Aperyの評価値の情報を標準出力へ出力します.
   * @param side_to_move 手番
   */
  void Print(const Color side_to_move) const;

  /** 駒割りに関する評価値. */
  int32_t material;

  /** KK（King-King）に関する評価値（駒の位置、手番）. */
  int32_t kk_board;
  //int32_t kk_turn;

  /** KKP（King-King-Piece）に関する評価値（駒の位置、手番）. */
  int32_t kkp_board;
  //int32_t kkp_turn;

  /** KPP（King-Piece-Piece）に関する評価値（駒の位置、手番）. */
  int32_t kpp_board[2];
  //int32_t kpp_turn [2];
};

/**
 * 評価値の詳細を保存するためのクラスです.
 * このように評価値の項目を細かく分類しておくと、必要な項目だけ差分計算することができるという利点があります。
 */
struct EvalDetail {
  EvalDetail operator+(const EvalDetail& rhs) const {
    return EvalDetail(*this) += rhs;
  }

  EvalDetail operator-(const EvalDetail& rhs) const {
    return EvalDetail(*this) -= rhs;
  }

  EvalDetail& operator+=(const EvalDetail& rhs) {
    kp[kBlack]  += rhs.kp[kBlack];
    kp[kWhite]  += rhs.kp[kWhite];
    controls    += rhs.controls;
    two_pieces  += rhs.two_pieces;
    king_safety += rhs.king_safety;
    sliders     += rhs.sliders;
    apery_eval_detail += rhs.apery_eval_detail;
    return *this;
  }

  EvalDetail& operator-=(const EvalDetail& rhs) {
    kp[kBlack]  -= rhs.kp[kBlack];
    kp[kWhite]  -= rhs.kp[kWhite];
    controls    -= rhs.controls;
    two_pieces  -= rhs.two_pieces;
    king_safety -= rhs.king_safety;
    sliders     -= rhs.sliders;
    apery_eval_detail -= rhs.apery_eval_detail;
    return *this;
  }

  /**
   * 局面の進行度と手番を考慮して、最終的な評価値を計算します.
   * @param side_to_move 手番
   * @param progress_output 進行度を出力するためのポインタ
   * @return 進行度と手番を考慮した、最終的な得点
   */
  // constを削除
  //Score ComputeFinalScore(Color side_to_move, double* progress_output = nullptr) const;
  Score ComputeFinalScore(Color side_to_move, double* progress_output = nullptr);

  /**
   * 評価値の詳細情報を標準出力へ出力します.
   * @param side_to_move 手番
   */
  // constを削除
  //void Print(Color side_to_move) const;
  void Print(Color side_to_move);

  /** KP（King-Piece）に関する評価値. */
  ArrayMap<PackedScore, Color> kp{PackedScore(0), PackedScore(0)};

  /** 利きに関する評価値. */
  PackedScore controls{0};

  /** ２駒の関係（PP: Piece-Piece）に関する評価値. */
  PackedScore two_pieces{0};

  /** 玉の安全度に関する評価値. */
  PackedScore king_safety{0};

  /** 飛び駒に関する評価値. */
  PackedScore sliders{0};

  /** Aperyの評価値. */
  AperyEvalDetail apery_eval_detail;


  // ----- 各々のソフトの最終的な評価値

  /** 技巧の評価値（最終的な計算結果）. */
  Score final_score_gikou;

  /** Aperyの評価値（最終的な計算結果）. */
  Score final_score_apery;

  // -----

};

/**
 * 評価値の計算を行うためのクラスです.
 */
class Evaluation {
 public:
  /**
   * 評価関数の初期化（評価パラメータのファイルからの読み込み等）を行います.
   */
  static void Init();

  static void ReadParametersFromFile(const char* file_name);

  /**
   * 局面の評価値を計算します.
   * @param pos 評価値を計算したい局面
   * @return その局面の評価値
   */
  static Score Evaluate(const Position& pos);

  /**
   * すべての評価項目について、評価値を計算します.
   * @param pos      評価値を計算したい局面
   * @param psq_list 駒の位置のインデックスのリスト
   * @return 評価項目ごとの評価値
   */
  static EvalDetail EvaluateAll(const Position& pos, const PsqList& psq_list);

  /**
   * 評価値の差分計算を行います.
   * @param pos           評価値を計算したい局面
   * @param previous_eval １手前の局面における、評価項目ごとの評価値
   * @param previous_list １手前の局面における、各マスごとの利きのインデックスのリスト
   * @param current_list  現在の局面における、各マスごとの利きのインデックスのリスト
   * @param psq_list      １手前の局面における、駒の位置のインデックスのリスト
   * @return 現局面における、評価項目ごとの評価値
   */
  static EvalDetail EvaluateDifference(const Position& pos,
                                       const EvalDetail& previous_eval,
                                       const PsqControlList& previous_list,
                                       const PsqControlList& current_list,
                                       PsqList* psq_list);

  /**
   * 評価値の詳細情報を標準出力へ出力します.
   * @param pos 評価値を計算したい局面
   */
  static void Print(const Position& pos);
};

/**
 * 評価値の計算に用いるパラメータ（重み）です.
 */
struct EvalParameters {
  /**
   * 評価パラメータをゼロクリアします.
   */
  void Clear() {
    std::memset(this, 0, sizeof(*this));
  }

  //
  // 1. 駒の価値
  //
  /** 駒の価値 [駒の種類] */
  ArrayMap<Score, PieceType> material;

  //
  // 2. ２駒の位置関係
  //
  /** 玉と玉以外の駒の位置関係 [玉の位置][玉以外の駒の種類及び位置] */
  ArrayMap<PackedScore, Square, PsqIndex> king_piece;
  /** 玉を除く２駒の位置関係 [駒１の種類及び位置][駒２の種類及び位置] */
  ArrayMap<PackedScore, PsqIndex, PsqIndex> two_pieces;

  //
  // 3. 各マスごとの利き
  //
  /** 各マスの利き [先手玉か後手玉か][玉の位置][マスの位置、駒の種類、先手の利き数、後手の利き数] */
  ArrayMap<PackedScore, Color, Square, PsqControlIndex> controls;

  //
  // 4. 玉の安全度
  //
  /** 玉の安全度 [相手の持ち駒][玉から見た方向][そのマスにある駒][攻め方の利き数][受け方の利き数] */
  ArrayMap<Array<PackedScore, 4, 4>, HandSet, Direction, Piece> king_safety;

  //
  // 5. 飛車・角・香車の利き
  //
  /** 飛車の利き [先手玉か後手玉か][玉の位置][飛車の位置][飛車の利きが届いている位置] */
  ArrayMap<PackedScore, Color, Square, Square, Square> rook_control;
  /** 角の利き [先手玉か後手玉か][玉の位置][角の位置][角の利きが届いている位置] */
  ArrayMap<PackedScore, Color, Square, Square, Square> bishop_control;
  /** 香車の利き [先手玉か後手玉か][玉の位置][香車の位置][香車の利きが届いている位置] */
  ArrayMap<PackedScore, Color, Square, Square, Square> lance_control;
  /** 飛車の利きが付いている駒　[相手玉の位置][飛車が利きをつけている駒の位置][飛車が利きをつけている駒] */
  ArrayMap<PackedScore, Square, Square, Piece> rook_threat;
  /** 角の利きが付いている駒　[相手玉の位置][角が利きをつけている駒の位置][角が利きをつけている駒] */
  ArrayMap<PackedScore, Square, Square, Piece> bishop_threat;
  /** 香車の利きが付いている駒　[相手玉の位置][香車が利きをつけている駒の位置][香車が利きをつけている駒] */
  ArrayMap<PackedScore, Square, Square, Piece> lance_threat;

  //
  // 6. 手番
  //
  /** 手番 */
  PackedScore tempo;
};

/**
 * 評価関数のパラメータを格納します.
 * evaluation.ccのみならず、学習用のコード（learning.cc等）でも使用するので、extern宣言を付けています。
 */
extern std::unique_ptr<EvalParameters> g_eval_params;


/**
 * Aperyの評価値関連
 */
namespace AperyEval {

  /** KP、KPP、KKPのスケール */
  const int FV_SCALE = 32;

  /** Aperyの駒割り */
  enum {
    PawnValue = 90,
    LanceValue = 315,
    KnightValue = 405,
    SilverValue = 495,
    GoldValue = 540,
    BishopValue = 855,
    RookValue = 990,
    ProPawnValue = 540,
    ProLanceValue = 540,
    ProKnightValue = 540,
    ProSilverValue = 540,
    HorseValue = 945,
    DragonValue = 1395,
    KingValue = 15000,
  };

  /** 駒の位置、手番 */
  //enum {
  //    kBoard
  //  , kTurn
  //};

  /**
   * 評価関数ファイルを読み込みます.
   */
  void LoadEval();

  /**
   * すべての評価項目について、評価値を計算します（全計算）.
   * @param pos      評価値を計算したい局面
   * @param psq_list 駒の位置のインデックスのリスト
   * @return 評価項目ごとの評価値
   */
  AperyEvalDetail ComputeEval(const Position& pos, const PsqList& list);

  /**
   * 駒割りを全計算します.
   * @param pos 評価値を計算したい局面
   * @return 駒割りの評価値
   */
  Score EvaluateMaterial(const Position& pos);

  /**
   * 駒割りを差分計算します.
   * @param pos 評価値を計算したい局面
   * @return 駒割りの評価値の差分
   */
  Score EvaluateDifferenceOfMaterial(const Position& pos);

  /**
   * Aperyの生の評価値を１歩＝１００点（centipawn）の評価値に変換します.
   * @param value 生の評価値
   * @return １歩＝１００点（centipawn）の評価値
   */
  double ToCentiPawn(const int32_t value);

  /**
   * Aperyの生の評価値を１歩＝１００点（centipawn）の評価値（手番側から見た評価値）に変換します.
   * @param value 生の評価値
   * @param side_to_move 手番
   * @return １歩＝１００点（centipawn）の評価値（手番側から見た評価値）
   */
  double ToCentiPawn(const int32_t value, const Color side_to_move);


} // namespace

#endif /* EVALUATION_H_ */
