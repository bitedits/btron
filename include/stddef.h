/*
 *----------------------------------------------------------------------
 *    T-Kernel 2.0 Software Package
 *
 *    Copyright 2011 by Ken Sakamura.
 *    This software is distributed under the latest version of T-License 2.x.
 *----------------------------------------------------------------------
 *
 *    Released by T-Engine Forum(http://www.t-engine.org/) at 2011/05/17.
 *    Modified by TRON Forum(http://www.tron.org/) at 2015/06/01.
 *
 *----------------------------------------------------------------------
 */

/*
 *	@(#)stddef.h
 *
 *	C language: common definitions
 */

#ifndef __STDDEF_H__
#define __STDDEF_H__

#if defined(_WIN32)
/* MinGW provides ABI-correct fundamental types on 64-bit Windows. */
#include_next <stddef.h>
#else

#include <stdtype.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int		ptrdiff_t;

#ifndef _SIZE_T
#ifndef _SIZE_T_DECLARED
#ifndef __size_t_defined
#define __size_t_defined
#define _SIZE_T
#define _SIZE_T_DECLARED
#if defined(__SIZE_TYPE__) && !defined(__size_t)
typedef __SIZE_TYPE__ size_t;
#elif defined(__size_t)
typedef __size_t	size_t;
#undef	__size_t
#endif
#endif
#endif
#endif

#ifdef	__wchar_t
typedef __wchar_t	wchar_t;
#undef	__wchar_t
#endif

#ifndef NULL
#define NULL	0
#endif

#ifndef offsetof
#if defined(__GNUC__) && __GNUC__ >= 4
#define offsetof(type, member)  __builtin_offsetof(type, member)
#else
#define offsetof(type, member)  ((size_t)&((type *)0)->member)
#endif
#endif


#if 1	/* wint_tコンパイルエラーの回避の為追加 */
#ifndef _WINT_T
#define _WINT_T
#ifndef _WINT_T_DECLARED
#define _WINT_T_DECLARED
#endif
#ifdef __WINT_TYPE__
typedef __WINT_TYPE__ wint_t;
#else
typedef int           wint_t;
#endif
#endif
#endif

#ifdef __cplusplus
}
#endif
#endif /* _WIN32 */
#endif /* __STDDEF_H__ */


/*----------------------------------------------------------------------
#|History of "stddef.h"
#|=====================
#|* 2017/09/07	Modified.(By T.Yokobayashi)
#|　Newlibなどコンパイラ側の標準ライブラリーを使う場合に、
#| 「wint_t」未定義のコンパイルエラーが発生するのでwint_tの定義を追加。
#|  コンパイラ標準のstddef.h呼ばれる所が、このファイルが優先されて呼び出され
#|  てしまう為に発生している。同じ名前のヘッダファイル名が使われてのは良くな
#|  いとは思うが、根本的な回避方法がわからなかったのでここに定義した。
#|
*/
