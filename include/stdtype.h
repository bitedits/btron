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
 *	@(#)stdtype.h
 *
 *	C language: standard type
 */

#ifndef __STDTYPE_H__
#define __STDTYPE_H__

#ifdef __SIZE_TYPE__
#define __size_t        __SIZE_TYPE__
#else
#define __size_t        unsigned int
#endif

#ifdef __PTRDIFF_TYPE__
#define __ptrdiff_t     __PTRDIFF_TYPE__
#elif defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__) || defined(_LP64)
#define __ptrdiff_t     long int
#else
#define __ptrdiff_t     int
#endif

#ifndef	__cplusplus
#define __wchar_t	int
#endif

#endif /* __STDTYPE_H__ */
