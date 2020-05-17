#ifndef _TYPES_H_INCLUDED
#define _TYPES_H_INCLUDED

// コンパイル時の設定などは以下のconfig.hを変更すること。
#include "config.h"


// 手番
constexpr int COLOR_NB = 2;

// 升目
constexpr int SQ_ZERO = 0;
constexpr int SQ_NB = 81;

// 駒
constexpr int NO_PIECE = 0;
constexpr int PIECE_TYPE_NB = 16;
constexpr int PIECE_NB = 32;

// TODO History配列のサイズ
constexpr int HISTORY_ARRAY_SIZE = 32;

// countermoves based pruningで使う閾値
constexpr int CounterMovePruneThreshold = 0;

// 将棋のある局面の合法手の最大数。593らしいが、保険をかけて少し大きめにしておく。
constexpr int MAX_MOVES = 600;

#endif // #ifndef _TYPES_H_INCLUDED
