#ifndef YANE_MOVEPICK_H_INCLUDED
#define YANE_MOVEPICK_H_INCLUDED

#include <array>
#include "types.h"

// -----------------------
//		history
// -----------------------

/// StatsEntryはstat tableの値を格納する。これは、大抵数値であるが、指し手やnestされたhistoryでさえありうる。
/// 多次元配列であるかのように呼び出し側でstats tablesを用いるために、
/// 生の値を用いる代わりに、history updateを行なうentry上でoperator<<()を呼び出す。

// T : このEntryの実体
// D : abs(entry) <= Dとなるように制限される。
template<typename T, int D>
class StatsEntry {

	// Tが整数型(16bitであろうと)ならIsIntはtrueになる。
	// IsIntがtrueであるとき、operator ()の返し値はint型としたいのでそのためのもの。
	static const bool IsInt = std::is_integral<T>::value;
	typedef typename std::conditional<IsInt, int, T>::type TT;

	T entry;

public:
	void operator=(const T& v) { entry = v; }
	T* operator&() { return &entry; }
	T* operator->() { return &entry; }
	operator const T&() const { return entry; }

	// このStatsEntry(Statsの1要素)に対して"<<"演算子でbonus値の加算が出来るようにしておく。
	// 値が範囲外にならないように制限してある。
	void operator<<(int bonus) {
		ASSERT_LV3(abs(bonus) <= D); // 範囲が[-D,D]であるようにする。
		// オーバーフローしないことを保証する。
		static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");
		
		entry += bonus - entry * abs(bonus) / D;

		ASSERT_LV3(abs(entry) <= D);
	}
};

/// Statsは、様々な統計情報を格納するために用いられる汎用的なN-次元配列である。
/// 1つ目のtemplate parameterであるTは、配列の基本的な型を示し、2つ目の
/// template parameterであるDは、<< operatorで値を更新するときに、値を[-D,D]の範囲に
/// 制限する。最後のparameter(SizeとSizes)は、配列の次元に用いられる。
template <typename T, int D, int Size, int... Sizes>
struct Stats : public std::array<Stats<T, D, Sizes...>, Size>
{
	T* get() { return this->at(0).get(); }

	void fill(const T& v) {
		T* p = get();
		std::fill(p, p + sizeof(*this) / sizeof(*p), v);
	}
};

template <typename T, int D, int Size>
struct Stats<T, D, Size> : public std::array<StatsEntry<T, D>, Size> {
	T* get() { return &this->at(0); }
};

// stats tableにおいて、Dを0にした場合、このtemplate parameterは用いないという意味。
enum StatsParams { NOT_USED = 0 };

enum StatsType { NoCaptures, Captures };

// ButterflyHistoryは、 現在の探索中にquietな指し手がどれくらい成功/失敗したかを記録し、
// reductionと指し手オーダリングの決定のために用いられる。
// cf. http://chessprogramming.wikispaces.com/Butterfly+Boards
// 簡単に言うと、fromの駒をtoに移動させることに対するhistory。
// やねうら王では、ここで用いられるfromは、駒打ちのときに特殊な値になっていて、盤上のfromとは区別される。
// そのため、(SQ_NB + 7)まで移動元がある。
// ※　Stockfishとは、添字の順番を入れ替えてあるので注意。
typedef Stats<int16_t, 10692, int(SQ_NB + 7) * int(SQ_NB), COLOR_NB> ButterflyHistory;

/// LowPlyHistory at higher depths records successful quiet moves on plies 0 to 3
/// and quiet moves which are/were in the PV (ttPv)
/// It get cleared with each new search and get filled during iterative deepening
constexpr int MAX_LPH = 4;
typedef Stats<int16_t, 10692, MAX_LPH, int(SQ_NB + 7) * int(SQ_NB)> LowPlyHistory;

/// CounterMoveHistoryは、直前の指し手の[to][piece]によってindexされるcounter moves(応手)を格納する。
/// cf. http://chessprogramming.wikispaces.com/Countermove+Heuristic
// ※　Stockfishとは、添字の順番を入れ替えてあるので注意。
typedef Stats<Move, NOT_USED, SQ_NB , PIECE_NB> CounterMoveHistory;

/// CapturePieceToHistoryは、指し手の[to][piece][captured piece type]で示される。
// ※　Stockfishとは、添字の順番を変更してあるので注意。
//    Stockfishでは、[piece][to][captured piece type]の順。
typedef Stats<int16_t, 10692, SQ_NB, PIECE_NB , PIECE_TYPE_NB> CapturePieceToHistory;

/// PieceToHistoryは、ButterflyHistoryに似たものだが、指し手の[to][piece]で示される。
// ※　Stockfishとは、添字の順番を入れ替えてあるので注意。
typedef Stats<int16_t, 29952, SQ_NB , PIECE_NB> PieceToHistory;

/// ContinuationHistoryは、与えられた2つの指し手のhistoryを組み合わせたもので、
// 普通、1手前によって与えられる現在の指し手(によるcombined history)
// このnested history tableは、ButterflyBoardsの代わりに、PieceToHistoryをベースとしている。
// ※　Stockfishとは、添字の順番を入れ替えてあるので注意。
typedef Stats<PieceToHistory, NOT_USED, SQ_NB , PIECE_NB> ContinuationHistory;


#endif // #ifndef YANE_MOVEPICK_H_INCLUDED
