#ifndef _EVALUATE_H_
#define _EVALUATE_H_

#include "types.h"
#include "../usi.h"

#define BonaPieceExpansion 0

namespace Eval {

	// 評価関数ファイルを読み込む。
	// これは、"is_ready"コマンドの応答時に1度だけ呼び出される。2度呼び出すことは想定していない。
	// (ただし、EvalDir(評価関数フォルダ)が変更になったあと、isreadyが再度送られてきたら読みなおす。)
	void load_eval(const UsiOptions& usi_options);

	// 評価関数本体
	Value evaluate(const Position& pos);

	// 評価関数本体
	// このあとのdo_move()のあとのevaluate()で差分計算ができるように、
	// 現在の前局面から差分計算ができるときだけ計算しておく。
	// 評価値自体は返さない。
	void evaluate_with_no_return(const Position& pos);

	// 駒割り以外の全計算して、その合計を返す。Position::set()で一度だけ呼び出される。
	// あるいは差分計算が不可能なときに呼び出される。
	Value compute_eval(const Position& pos);


	// BonanzaでKKP/KPPと言うときのP(Piece)を表現する型。
	// Σ KPPを求めるときに、39の地点の歩のように、升×駒種に対して一意な番号が必要となる。
	enum BonaPiece : int32_t
	{
		// f = friend(≒先手)の意味。e = enemy(≒後手)の意味

		// 未初期化の時の値
		BONA_PIECE_NOT_INIT = -1,

		// 無効な駒。駒落ちのときなどは、不要な駒をここに移動させる。
		BONA_PIECE_ZERO = 0,

		// --- 手駒

#if defined (EVAL_MATERIAL) || defined (EVAL_KPPT) || defined(EVAL_KPP_KKPT) || defined(EVAL_NNUE)
		// Apery(WCSC26)方式。0枚目の駒があるので少し隙間がある。
		// 定数自体は1枚目の駒のindexなので、EVAL_KPPの時と同様の処理で問題ない。
		// 例)
		//  f_hand_pawn  = 先手の1枚目の手駒歩
		//  e_hand_lance = 後手の1枚目の手駒の香
		// Aperyとは手駒に関してはこの部分の定数の意味が1だけ異なるので注意。

		f_hand_pawn = BONA_PIECE_ZERO + 1,//0//0+1
		e_hand_pawn = 20,//f_hand_pawn + 19,//19+1
		f_hand_lance = 39,//e_hand_pawn + 19,//38+1
		e_hand_lance = 44,//f_hand_lance + 5,//43+1
		f_hand_knight = 49,//e_hand_lance + 5,//48+1
		e_hand_knight = 54,//f_hand_knight + 5,//53+1
		f_hand_silver = 59,//e_hand_knight + 5,//58+1
		e_hand_silver = 64,//f_hand_silver + 5,//63+1
		f_hand_gold = 69,//e_hand_silver + 5,//68+1
		e_hand_gold = 74,//f_hand_gold + 5,//73+1
		f_hand_bishop = 79,//e_hand_gold + 5,//78+1
		e_hand_bishop = 82,//f_hand_bishop + 3,//81+1
		f_hand_rook = 85,//e_hand_bishop + 3,//84+1
		e_hand_rook = 88,//f_hand_rook + 3,//87+1
		fe_hand_end = 90,//e_hand_rook + 3,//90

#else 
		fe_hand_end = 0,
#endif                     

		// Bonanzaのように盤上のありえない升の歩や香の番号を詰めない。
		// 理由1) 学習のときに相対PPで1段目に香がいるときがあって、それを逆変換において正しく表示するのが難しい。
		// 理由2) 縦型BitboardだとSquareからの変換に困る。

		// --- 盤上の駒
		f_pawn = fe_hand_end,
		e_pawn = f_pawn + 81,
		f_lance = e_pawn + 81,
		e_lance = f_lance + 81,
		f_knight = e_lance + 81,
		e_knight = f_knight + 81,
		f_silver = e_knight + 81,
		e_silver = f_silver + 81,
		f_gold = e_silver + 81,
		e_gold = f_gold + 81,
		f_bishop = e_gold + 81,
		e_bishop = f_bishop + 81,
		f_horse = e_bishop + 81,
		e_horse = f_horse + 81,
		f_rook = e_horse + 81,
		e_rook = f_rook + 81,
		f_dragon = e_rook + 81,
		e_dragon = f_dragon + 81,
		fe_old_end = e_dragon + 81,

		// fe_endの値をBonaPieceExpansionを定義することで変更できる。
		// このときfe_old_end～fe_endの間の番号をBonaPiece拡張領域として自由に用いることが出来る。
		fe_end = fe_old_end + BonaPieceExpansion,

		// fe_end がKPP配列などのPの値の終端と考えられる。
		// 例) kpp[SQ_NB][fe_end][fe_end];

		// 王も一意な駒番号を付与。これは2駒関係をするときに王に一意な番号が必要なための拡張
		f_king = fe_end,
		e_king = f_king + SQ_NB,
		fe_end2 = e_king + SQ_NB, // 玉も含めた末尾の番号。

		// 末尾は評価関数の性質によって異なるので、BONA_PIECE_NBを定義するわけにはいかない。
	};

	// BonaPieceの内容を表示する。手駒ならH,盤上の駒なら升目。例) HP3 (3枚目の手駒の歩)
	std::ostream& operator<<(std::ostream& os, BonaPiece bp);

	// BonaPieceを後手から見たとき(先手の39の歩を後手から見ると後手の71の歩)の番号とを
	// ペアにしたものをExtBonaPiece型と呼ぶことにする。
	union ExtBonaPiece
	{
		struct {
			BonaPiece fb; // from black
			BonaPiece fw; // from white
		};
		BonaPiece from[2];

		ExtBonaPiece() {}
		ExtBonaPiece(BonaPiece fb_, BonaPiece fw_) : fb(fb_) , fw(fw_){}
	};


	// 駒が今回の指し手によってどこからどこに移動したのかの情報。
	// 駒はExtBonaPiece表現であるとする。
	struct ChangedBonaPiece
	{
		ExtBonaPiece old_piece;
		ExtBonaPiece new_piece;
	};


	// --- 局面の評価値の差分更新用
	// 局面の評価値を差分更新するために、移動した駒を管理する必要がある。
	// この移動した駒のことをDirtyPieceと呼ぶ。
	// 1) FV38方式だと、DirtyPieceはたかだか2個。
	// 2) FV_VAR方式だと、DirtyPieceは可変。

#if defined (USE_FV38)

	// 評価値の差分計算の管理用
	// 前の局面から移動した駒番号を管理するための構造体
	// 動く駒は、最大で2個。
	struct DirtyPiece
	{
		// その駒番号の駒が何から何に変わったのか
		Eval::ChangedBonaPiece changed_piece[2];

		// dirtyになった駒番号
		PieceNumber pieceNo[2];

		// dirtyになった個数。
		// null moveだと0ということもありうる。
		// 動く駒と取られる駒とで最大で2つ。
		int dirty_num;

};
#endif

}


#endif // #ifndef _EVALUATE_H_
