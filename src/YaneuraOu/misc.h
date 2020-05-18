#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

<<<<<<< HEAD
//#include <chrono>
//#include <vector>
//#include <functional>
//#include <fstream>
#include <iostream>

//#include "types.h"
#include "thread_win32.h" // AsyncPRNGで使う


// --------------------
//  sync_out/sync_endl
// --------------------

// スレッド排他しながらcoutに出力するために使う。
// 例)
// sync_out << "bestmove " << m << sync_endl;
// のように用いる。

enum SyncCout { IO_LOCK, IO_UNLOCK };
std::ostream& operator<<(std::ostream&, SyncCout);

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK


// --------------------
//   以下は、やねうら王の独自追加
// --------------------

// 指定されたミリ秒だけsleepする。
extern void sleep(int ms);

// 現在時刻を文字列化したもを返す。(評価関数の学習時などにログ出力のために用いる)
std::string now_string();


// 途中での終了処理のためのwrapper
static void my_exit()
{
	sleep(3000); // エラーメッセージが出力される前に終了するのはまずいのでwaitを入れておく。
	exit(EXIT_FAILURE);
}


=======
>>>>>>> origin/m
// --------------------
//       Math
// --------------------

// 進行度の計算や学習で用いる数学的な関数
namespace Math {
<<<<<<< HEAD

=======
>>>>>>> origin/m
	// vを[lo,hi]の間に収まるようにクリップする。
	// ※　Stockfishではこの関数、bitboard.hに書いてある。
	template<class T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
		return v < lo ? lo : v > hi ? hi : v;
	}
}

<<<<<<< HEAD

// --------------------
//       Path
// --------------------

// C#にあるPathクラス的なもの。ファイル名の操作。
// C#のメソッド名に合わせておく。
struct Path
{
	// path名とファイル名を結合して、それを返す。
	// folder名のほうは空文字列でないときに、末尾に'/'か'\\'がなければそれを付与する。
	static std::string Combine(const std::string& folder, const std::string& filename)
	{
		if (folder.length() >= 1 && *folder.rbegin() != '/' && *folder.rbegin() != '\\')
			return folder + "/" + filename;

		return folder + filename;
	}
};

=======
>>>>>>> origin/m
#endif // #ifndef MISC_H_INCLUDED
