/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
 *
 * The Search class below is derived from Stockfish 7.
 * Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
 * Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish author)
 * Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish author)
 * Copyright (C) 2016-2017 Yosuke Demura
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

#ifndef SEARCH_H_
#define SEARCH_H_

#include <atomic>
#include <vector>
#include <utility>
#include "common/array.h"
#include "common/arraymap.h"
#include "move.h"
#include "node.h"
#include "pvtable.h"
#include "shared_data.h"
#include "stats.h"
#include "YaneuraOu/movepick.h"

class ThreadManager;

/**
 * アルファベータ探索の並列探索で使用する、最大スレッド数です.
 */
constexpr int kMaxSearchThreads = 64;

/**
 * アルファベータ探索を行うためのクラスです.
 */
class Search {
 public:
  enum NodeType {
    kRootNode, kPvNode, kNonPvNode,
  };

  struct Stack {
    Array<Move, 2> killers;
    Move hash_move;
    Move current_move;
    HistoryStats* countermoves_history;
    Move excluded_move;
    //Depth reduction;
    Score static_score;
    //bool skip_null_move;

    // やねうら王
    PieceToHistory* continuationHistory;  // historyのうち、counter moveに関するhistoryへのポインタ。
    int ply;                              // rootからの手数。rootならば0。
    int statScore;                        // 一度計算したhistoryの合計値をcacheしておくのに用いる。
    int moveCount;                        // このnodeでdo_move()した生成した何手目の指し手か。(1ならおそらく置換表の指し手だろう)
    bool inCheck;
  };

  static void Init();

  Search(SharedData& shared, size_t thread_id = 0);

  /**
   * 反復深化による探索を行います.
   * 注意：この関数を呼ぶ前に、予めset_root_moves()関数を呼び、root_moves_をセットしておいてください。
   */
  void IterativeDeepening(Node& node, ThreadManager& thread_manager);

  void set_root_moves(const std::vector<RootMove>& root_moves) {
    root_moves_ = root_moves;
  }

  static std::vector<RootMove> CreateRootMoves(const Position& root_position,
                                               const std::vector<Move>& searchmoves,
                                               const std::vector<Move>& ignoremoves);


  Score AlphaBetaSearch(Node& node, Score alpha, Score beta, Depth depth);
  Score NullWindowSearch(Node& node, Score alpha, Score beta, Depth depth);

  /**
   * シンプルな反復深化探索を行います.
   * 主に教師局面の生成に用いることを想定しています.
   * @param pos 探索を行う局面
   * @return 最善手と評価値のペア
   */
  std::pair<Move, Score> SimpleIterativeDeepening(const Position& pos);

  template<NodeType kNodeType>
  Score MainSearch(Node& node, Score alpha, Score beta, Depth depth, int ply,
                   bool cut_node);

  bool is_master_thread() const {
    return thread_id_ == 0;
  }

  uint64_t num_nodes_searched() const {
    return num_nodes_searched_;
  }

  void set_learning_mode(bool is_learning) {
    learning_mode_ = is_learning;
  }

  void set_draw_scores(Color root_side_to_move, Score draw_score) {
    assert(-kScoreKnownWin < draw_score && draw_score < kScoreKnownWin);
    draw_scores_[root_side_to_move] = draw_score;
    draw_scores_[~root_side_to_move] = -draw_score;
  }

  void set_multipv(int multipv) {
    multipv_ = std::max(multipv, 1);
  }

  void set_nodes_limit(uint64_t nodes) {
    nodes_limit_ = nodes;
  }

  void set_depth_limit(int depth_limit) {
    depth_limit_ = std::min(std::max(depth_limit, 1), kMaxPly);
  }

  std::vector<Move> GetPv() const;

  const RootMove& GetBestRootMove() const;

  uint64_t GetNodesUnder(Move move) const;

  const PvTable& pv_table() const {
    return pv_table_;
  }

  const HistoryStats& history() const {
    return history_;
  }

  const GainsStats& gains() const {
    return gains_;
  }

  void PrepareForNextSearch(int num_search_threads = 1);


  // やねうら王（Stockfish11）のHistory
  CounterMoveHistory* counterMoves_;
  ButterflyHistory* mainHistory_;
  LowPlyHistory* lowPlyHistory_;
  CapturePieceToHistory* captureHistory_;
  ContinuationHistory (*continuationHistory_)[2][2];

  uint64_t ttHitAverage_;

  // nmpMinPly : null moveの前回の適用ply
  // nmpColor  : null moveの前回の適用Color
  int nmpMinPly_;
  Color nmpColor_;


 private:
  static constexpr int kStackSize = kMaxPly + 10;

  template<NodeType kNodeType>
  Score QuiecenceSearch(Node& node, Score alpha, Score beta, Depth depth,
                        int ply) {
    return node.in_check()
         ? QuiecenceSearch<kNodeType, true >(node, alpha, beta, depth, ply)
         : QuiecenceSearch<kNodeType, false>(node, alpha, beta, depth, ply);
  }

  template<NodeType kNodeType, bool kInCheck>
  Score QuiecenceSearch(Node& node, Score alpha, Score beta, Depth depth,
                        int ply);

  void UpdateStats(Search::Stack* ss, Move move, Depth depth, Move* quiets,
                   int quiets_count);

  void update_all_stats(const Node& pos, Stack* ss, Move bestMove, Score bestValue, Score beta, Square prevSq,
                        Move* quietsSearched, int quietCount, Move* capturesSearched, int captureCount, Depth depth);

  void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus);

  void update_quiet_stats(const Node& pos, Stack* ss, Move move, int bonus, Depth depth);

  void SendUsiInfo(const Node& node, int depth, int64_t time, uint64_t nodes,
                   Bound bound = kBoundExact) const;

  void ResetSearchStack() {
    std::memset(stack_.begin(), 0, 10 * sizeof(Stack));

    // counterMovesをnullptrに初期化するのではなくNO_PIECEのときの値を番兵として用いる。
    for (int i = 0; i < 7; i++) {
      (stack_.begin() + i)->continuationHistory = &(*this->continuationHistory_)[0][0][SQ_ZERO][NO_PIECE];
    }
  }

  Stack* search_stack_at_ply(int ply) {
    assert(0 <= ply && ply <= kMaxPly);
    return stack_.begin() + 6 + ply; // stack_at_ply(0) - 6 の参照を可能にするため
  }

  SharedData& shared_;
  ArrayMap<Score, Color> draw_scores_{kScoreDraw, kScoreDraw};
  uint64_t num_nodes_searched_ = 0;
  int max_reach_ply_ = 0;
  int multipv_ = 1, pv_index_ = 0;
  bool learning_mode_ = false;
  Array<Stack, kStackSize> stack_;
  PvTable pv_table_;

  HistoryStats history_;
  MovesStats countermoves_;
  MovesStats followupmoves_;
  GainsStats gains_;
  std::vector<RootMove> root_moves_;

  int depth_limit_ = kMaxPly;
  uint64_t nodes_limit_ = UINT64_MAX;

  const size_t thread_id_;
};

#endif /* SEARCH_H_ */
