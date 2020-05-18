#ifndef _TYPES_H_INCLUDED
#define _TYPES_H_INCLUDED

// --------------------
// release configurations
// --------------------

// コンパイル時の設定などは以下のconfig.hを変更すること。
#include "config.h"

// --------------------
//      include
// --------------------

// あまりたくさんここに書くとコンパイルが遅くなるので書きたくないのだが…。

#include "extra/bitop.h"
#include "../square.h"
#include "../common/operators.h"


// --------------------
//  operators and macros
// --------------------

#include "extra/macros.h"


// ----------------------------
//      type define(uint)
// ----------------------------
typedef  uint8_t  u8;
typedef   int8_t  s8;
typedef uint16_t u16;
typedef  int16_t s16;
typedef uint32_t u32;
typedef  int32_t s32;
typedef uint64_t u64;
typedef  int64_t s64;


// --------------------
//        駒
// --------------------
enum YanePiece : uint32_t
{
	// 金の順番を飛の後ろにしておく。KINGを8にしておく。
	// こうすることで、成りを求めるときに pc |= 8;で求まり、かつ、先手の全種類の駒を列挙するときに空きが発生しない。(DRAGONが終端になる)
	NO_PIECE, PAWN/*歩*/, LANCE/*香*/, KNIGHT/*桂*/, SILVER/*銀*/, BISHOP/*角*/, ROOK/*飛*/, GOLD/*金*/,
	KING = 8/*玉*/, PRO_PAWN /*と*/, PRO_LANCE /*成香*/, PRO_KNIGHT /*成桂*/, PRO_SILVER /*成銀*/, HORSE/*馬*/, DRAGON/*龍*/, QUEEN/*未使用*/,
	// 以下、先後の区別のある駒(Bがついているのは先手、Wがついているのは後手)
	B_PAWN = 1, B_LANCE, B_KNIGHT, B_SILVER, B_BISHOP, B_ROOK, B_GOLD, B_KING, B_PRO_PAWN, B_PRO_LANCE, B_PRO_KNIGHT, B_PRO_SILVER, B_HORSE, B_DRAGON, B_QUEEN,
	W_PAWN = 17, W_LANCE, W_KNIGHT, W_SILVER, W_BISHOP, W_ROOK, W_GOLD, W_KING, W_PRO_PAWN, W_PRO_LANCE, W_PRO_KNIGHT, W_PRO_SILVER, W_HORSE, W_DRAGON, W_QUEEN,
	PIECE_NB, // 終端
	PIECE_ZERO = 0,
};

ENABLE_ADD_AND_SUBTRACT_OPERATORS(YanePiece)
ENABLE_RANGE_OPERATORS_ON(YanePiece, PIECE_ZERO, PIECE_NB)


// --------------------
//        駒箱
// --------------------

// Positionクラスで用いる、駒リスト(どの駒がどこにあるのか)を管理するときの番号。
enum PieceNumber : u8
{
  //PIECE_NUMBER_PAWN = 0, PIECE_NUMBER_LANCE = 18, PIECE_NUMBER_KNIGHT = 22, PIECE_NUMBER_SILVER = 26,
  //PIECE_NUMBER_GOLD = 30, PIECE_NUMBER_BISHOP = 34, PIECE_NUMBER_ROOK = 36, 
  PIECE_NUMBER_KING = 38,
  PIECE_NUMBER_BKING = 38, PIECE_NUMBER_WKING = 39, // 先手、後手の玉の番号が必要な場合はこっちを用いる
  PIECE_NUMBER_ZERO = 0, PIECE_NUMBER_NB = 40,
};


class Position; // 前方宣言


// 手番
//constexpr int COLOR_NB = 2;

// 升目
constexpr int SQ_ZERO = 0;
constexpr int SQ_NB = 81;

// 駒
//constexpr int NO_PIECE = 0;
constexpr int PIECE_TYPE_NB = 16;
//constexpr int PIECE_NB = 32;

// TODO History配列のサイズ
constexpr int HISTORY_ARRAY_SIZE = 32;

// countermoves based pruningで使う閾値
constexpr int CounterMovePruneThreshold = 0;

// 将棋のある局面の合法手の最大数。593らしいが、保険をかけて少し大きめにしておく。
constexpr int MAX_MOVES = 600;


#endif // #ifndef _TYPES_H_INCLUDED
