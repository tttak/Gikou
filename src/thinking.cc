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

#include "thinking.h"

#include "book.h"
#include "movegen.h"
#include "node.h"
#include "search.h"
#include "synced_printf.h"
#include "thread.h"
#include "usi.h"
#include "usi_protocol.h"


// 探索で実現確率を使用する深さの最小値
extern int g_UseProbabilityMinDepth;

// Stockfish7のStats
extern SfHistoryStats g_arySfHistory[8];
extern CounterMoveHistoryStats g_aryCounterMoveHistory[8];
extern FromToStats g_aryFromTo[8];

// ----- 評価関数のmix

// 混ぜ合わせる評価関数ファイル
std::string g_MixGikouEvalFile;
// 評価値を混ぜる割合（単位は%）
int g_MixGikouEvalRate;

// ----- 評価値の割合（序盤、終盤）（単位は%）

// 評価値の割合【KP】（序盤、終盤）（単位は%）
int g_EvalKPJoban;
int g_EvalKPShuban;

// 評価値の割合【PP】（序盤、終盤）（単位は%）
int g_EvalPPJoban;
int g_EvalPPShuban;

// 評価値の割合【利き（各マスの利き）】（序盤、終盤）（単位は%）
int g_EvalControlsJoban;
int g_EvalControlsShuban;

// 評価値の割合【玉の安全度】（序盤、終盤）（単位は%）
int g_EvalKingSafetyJoban;
int g_EvalKingSafetyShuban;

// 評価値の割合【飛び駒（飛車・角・香車の利き）】（序盤、終盤）（単位は%）
int g_EvalSlidersJoban;
int g_EvalSlidersShuban;

// -----

namespace {

const char* kBookFile = "book.bin";

} // namespace

Thinking::Thinking(const UsiOptions& usi_options)
    : usi_options_(usi_options),
      time_manager_(usi_options, &shared_data_.signals),
      thread_manager_(shared_data_, time_manager_) {
}

void Thinking::Initialize() {
  book_.ReadFromFile(kBookFile);
  shared_data_.hash_table.SetSize(usi_options_["USI_Hash"]);

  // 探索で実現確率を使用する深さの最小値
  g_UseProbabilityMinDepth = usi_options_["Z01_UseProbabilityMinDepth"];

  // Stockfish7のStatsのクリア
  // TODO とりあえず配列の要素数は8固定。あとでスレッド数に応じて変更できるようにする予定。
  for (int i = 0; i < 8; i++) {
    g_arySfHistory[i].clear();
    g_aryCounterMoveHistory[i].clear();
    g_aryFromTo[i].clear();
  }

  // ----- 評価関数のmix

  // 混ぜ合わせる評価関数ファイル
  g_MixGikouEvalFile = usi_options_["Z02_MixGikouEvalFile"].str_value();
  // 評価値を混ぜる割合（単位は%）
  g_MixGikouEvalRate = usi_options_["Z03_MixGikouEvalRate"];

  // ----- 評価値の割合（序盤、終盤）（単位は%）

  // 評価値の割合【KP】（序盤、終盤）（単位は%）
  g_EvalKPJoban  = usi_options_["Z1A_EvalKPJoban"];
  g_EvalKPShuban = usi_options_["Z1B_EvalKPShuban"];

  // 評価値の割合【PP】（序盤、終盤）（単位は%）
  g_EvalPPJoban  = usi_options_["Z2A_EvalPPJoban"];
  g_EvalPPShuban = usi_options_["Z2B_EvalPPShuban"];

  // 評価値の割合【利き（各マスの利き）】（序盤、終盤）（単位は%）
  g_EvalControlsJoban  = usi_options_["Z3A_EvalControlsJoban"];
  g_EvalControlsShuban = usi_options_["Z3B_EvalControlsShuban"];

  // 評価値の割合【玉の安全度】（序盤、終盤）（単位は%）
  g_EvalKingSafetyJoban  = usi_options_["Z4A_EvalKingSafetyJoban"];
  g_EvalKingSafetyShuban = usi_options_["Z4B_EvalKingSafetyShuban"];

  // 評価値の割合【飛び駒（飛車・角・香車の利き）】（序盤、終盤）（単位は%）
  g_EvalSlidersJoban  = usi_options_["Z5A_EvalSlidersJoban"];
  g_EvalSlidersShuban = usi_options_["Z5B_EvalSlidersShuban"];

  // -----
}

void Thinking::StartNewGame() {
  // 現在のところ、特に行う処理はない
}

void Thinking::ResetSignals() {
  shared_data_.signals.Reset();

  // 「go ponder」の直後に「ponderhit」が来ても大丈夫なように修正
  time_manager_.set_ponderhit(false);
}

void Thinking::StartThinking(const Node& root_node,
                             const UsiGoOptions& go_options) {
  bool win_declaration_is_possible = false;
  Move best_move = kMoveNone;
  Move ponder_move = kMoveNone;
  SimpleMoveList<kAllMoves, true> all_legal_moves(root_node);

  // 1. 入玉宣言勝ち
  if (root_node.WinDeclarationIsPossible(true)) {
    win_declaration_is_possible = true;
    SYNCED_PRINTF("info depth 1 nodes 0 time 0 score mate + string Nyugyoku\n");
    goto send_best_move;
  }

  // 2. 合法手が存在しなければ、投了する
  if (all_legal_moves.size() == 0) {
    best_move = kMoveNone;
    goto send_best_move;
  }

  // 3. 時間制限が存在する場合は、定跡を使う
  if (   !go_options.infinite
      && !go_options.ponder
      && usi_options_["OwnBook"]
      && root_node.game_ply() + 1 <= usi_options_["BookMaxPly"]) {
    // 定跡DBから１手取得する
    Move book_move = book_.GetOneBookMove(root_node, usi_options_);

    // 定跡存在するときは、通常探索をスキップする
    if (book_move != kMoveNone) {
      best_move = book_move;
      goto send_best_move;
    }
  }

  // 4. 通常探索を行う
  if (!go_options.mate) {
    // a. 時間管理を開始する
    time_manager_.StartTimeManagement(root_node, go_options);

    // b. 探索の準備をする
    Node node = root_node;
    Score draw_score = Score(int(usi_options_["DrawScore"]));
    thread_manager_.SetNumSearchThreads(usi_options_["Threads"]);

    // c. 探索を開始する
    const RootMove& best_root_move = thread_manager_.ParallelSearch(node,
                                                                    draw_score,
                                                                    go_options,
                                                                    usi_options_["MultiPV"]);

    // d. 時間管理用のスレッドに終了の指示を出す
    time_manager_.StopTimeManagement();

    // e. 最善手と、相手の予想手を取得する
    const std::vector<Move>& pv = best_root_move.pv;
    best_move   = pv.size() >= 1U ? pv.at(0) : kMoveNone;
    ponder_move = pv.size() >= 2U ? pv.at(1) : kMoveNone;

    // f. 相手の予想手が取得できない場合は、ハッシュテーブルからの取得を試みる
    if (ponder_move == kMoveNone) {
      // note: まれに、ハッシュテーブルからも取得できない場合もある
      ponder_move = shared_data_.hash_table.GetPonderMove(root_node, best_move);
    }

    // g. 時間管理用のスレッドが終了するまで待機する
    time_manager_.WaitUntilTaskIsFinished();
  }

send_best_move:

  // 5. 必要であれば、最善手を送る前に待機する
  //    USIプロトコルにおいては、go infiniteか、go ponderで始まった場合は、
  //    stopかponderhitが来ない限り、bestmoveを返してはいけないことになっているため。
  //    http://www.geocities.jp/shogidokoro/usi.html
  if (go_options.infinite || go_options.ponder) {
    std::unique_lock<std::mutex> lock(mutex_);
    sleep_condition_.wait(lock, [&](){
      return shared_data_.signals.stop || shared_data_.signals.ponderhit;
    });
  }

  // 6. 最善手を送る
  if (win_declaration_is_possible) {
    // a. 入玉宣言勝ちができる場合は、勝ち宣言を行う
    SYNCED_PRINTF("bestmove win\n");
  } else if (best_move == kMoveNone) {
    // b. 最善手がなければ投了する
    SYNCED_PRINTF("bestmove resign\n");
  } else if (usi_options_["USI_Ponder"] && ponder_move != kMoveNone) {
    // c. 最善手と、予測読みの手を送る
    SYNCED_PRINTF("bestmove %s ponder %s\n",
                  best_move.ToSfen().c_str(),
                  ponder_move.ToSfen().c_str());
  } else {
    // d. 最善手のみを送る
    SYNCED_PRINTF("bestmove %s\n", best_move.ToSfen().c_str());
  }
}

void Thinking::StopThinking() {
  mutex_.lock();
  shared_data_.signals.stop = true;
  mutex_.unlock();

  sleep_condition_.notify_one();
}

void Thinking::Ponderhit() {
  time_manager_.RecordPonderhitTime();

  mutex_.lock();
  shared_data_.signals.ponderhit = true;
  mutex_.unlock();

  sleep_condition_.notify_one();
}
