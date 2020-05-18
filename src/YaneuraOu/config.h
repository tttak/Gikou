#ifndef _CONFIG_H_INCLUDED
#define _CONFIG_H_INCLUDED

<<<<<<< HEAD
#include <cstdint>
#include "../types.h"


// --------------------
// コンパイル時設定
// --------------------

#define EVAL_NNUE
#define USE_FV38
#define PRETTY_JP

#define USE_AVX2
#define USE_SSE42
#define USE_SSE41
#define USE_SSE2


=======
>>>>>>> origin/m
// --------------------
//      configure
// --------------------

<<<<<<< HEAD
// どちらかをdefineする。
//#define EVAL_NNUE_HALFKP
#define EVAL_NNUE_HALFKPE9


// デバッグ用
#define ENABLE_TEST_CMD
//#define ASSERT_LV 5
//#define USE_DEBUG_ASSERT
=======
// デバッグ用
//#define ASSERT_LV 5
>>>>>>> origin/m


// --- assertion tools

// DEBUGビルドでないとassertが無効化されてしまうので無効化されないASSERT
// 故意にメモリアクセス違反を起こすコード。
// USE_DEBUG_ASSERTが有効なときには、ASSERTの内容を出力したあと、3秒待ってから
// アクセス違反になるようなコードを実行する。
#if !defined (USE_DEBUG_ASSERT)
#define ASSERT(X) { if (!(X)) *(int*)1 = 0; }
#else
#include <iostream>
#include <chrono>
#include <thread>
#define ASSERT(X) { if (!(X)) { std::cout << "\nError : ASSERT(" << #X << "), " << __FILE__ << "(" << __LINE__ << "): " << __func__ << std::endl; \
 std::this_thread::sleep_for(std::chrono::microseconds(3000)); *(int*)1 =0;} }
#endif

// ASSERT LVに応じたassert
#ifndef ASSERT_LV
#define ASSERT_LV 0
#endif

#define ASSERT_LV_EX(L, X) { if (L <= ASSERT_LV) ASSERT(X); }
#define ASSERT_LV1(X) ASSERT_LV_EX(1, X)
#define ASSERT_LV2(X) ASSERT_LV_EX(2, X)
#define ASSERT_LV3(X) ASSERT_LV_EX(3, X)
#define ASSERT_LV4(X) ASSERT_LV_EX(4, X)
#define ASSERT_LV5(X) ASSERT_LV_EX(5, X)


#endif // ifndef _CONFIG_H_INCLUDED
<<<<<<< HEAD

=======
>>>>>>> origin/m
