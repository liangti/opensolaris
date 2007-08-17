
/* : : generated by proto : : */
/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*           Copyright (c) 1985-2007 AT&T Knowledge Ventures            *
*                      and is licensed under the                       *
*                  Common Public License, Version 1.0                  *
*                      by AT&T Knowledge Ventures                      *
*                                                                      *
*                A copy of the License is available at                 *
*            http://www.opensource.org/licenses/cpl1.0.txt             *
*         (with md5 checksum 059e8cd6165cb4c31e351f2b69388fd9)         *
*                                                                      *
*              Information and Software Systems Research               *
*                            AT&T Research                             *
*                           Florham Park NJ                            *
*                                                                      *
*                 Glenn Fowler <gsf@research.att.com>                  *
*                  David Korn <dgk@research.att.com>                   *
*                   Phong Vo <kpv@research.att.com>                    *
*                                                                      *
***********************************************************************/
                  
/*
 * Glenn Fowler
 * AT&T Research
 *
 * mounted filesystem scan interface
 */

#ifndef _MNT_H
#if !defined(__PROTO__)
#include <prototyped.h>
#endif
#if !defined(__LINKAGE__)
#define __LINKAGE__		/* 2004-08-11 transition */
#endif

#define _MNT_H	1

#undef	MNT_REMOTE			/* aix clash			*/
#define MNT_REMOTE	(1<<0)		/* remote mount			*/

typedef struct
{
	char*	fs;			/* filesystem name		*/
	char*	dir;			/* mounted dir			*/
	char*	type;			/* filesystem type		*/
	char*	options;		/* options			*/
	int	freq;			/* backup frequency		*/
	int	npass;			/* number of parallel passes	*/
	int	flags;			/* MNT_* flags			*/
} Mnt_t;

#if _BLD_ast && defined(__EXPORT__)
#undef __MANGLE__
#define __MANGLE__ __LINKAGE__		__EXPORT__
#endif

extern __MANGLE__ __V_*	mntopen __PROTO__((const char*, const char*));
extern __MANGLE__ Mnt_t*	mntread __PROTO__((__V_*));
extern __MANGLE__ int	mntwrite __PROTO__((__V_*, const Mnt_t*));
extern __MANGLE__ int	mntclose __PROTO__((__V_*));

#undef __MANGLE__
#define __MANGLE__ __LINKAGE__

#endif
