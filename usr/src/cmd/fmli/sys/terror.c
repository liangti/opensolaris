/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright  (c) 1986 AT&T
 *	All Rights Reserved
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include	<stdio.h>
#include	<errno.h>
#include	<sys/types.h>
#include	"wish.h"
#include	"message.h"
#include	"vtdefs.h"
#include	"terror.h"
#include	"retcodes.h"
#include	"sizes.h"

extern char	*Errlist[];
extern char	*What[];
extern int	Vflag;
extern void	exit();		/* fmli's exit not the C lib call */
static void log();
static void notify();

void
_terror(sev, what, name, file, line, child)
int	sev;	/* flags to see if we should log or exit */
int	what;	/* What we were trying to do */
char	*name;	/* What we were trying to do it to */
char	*file;	/* __FILE__ */
int	line;	/* __LINE__ */
bool 	child;	/* TRUE if called by a child of fmli. abs k15 */
{
	if (errno == ENOTTY)
		errno = 0;
	notify(what, child);	/* abs k15 */
	if (Vflag && (sev & TERR_LOG))
		log(sev, what, name, file, line);
	if (sev & TERR_EXIT)
		exit(R_ERR);	/* fmli's exit not the C lib call */
	errno = 0;
}

static void
notify(what, child)
int	what;
bool 	child;	 /* TRUE if called by a child of fmli. abs k15 */
{
	register char	*mymess;
	register int	length;
	char	messbuf[PATHSIZ];
	extern bool	Use_errno[];
	char	*push_win();

	if ((mymess = What[what]) == nil &&
	    (Use_errno[what] == FALSE || Errlist[errno] == nil))
		return;
	if (mymess == nil)
		mymess = "error";

	strncpy(messbuf, mymess, PATHSIZ - 1);
	length = strlen(messbuf);
	if (Use_errno[what] && Errlist[errno] != nil) {
		strncat(messbuf, ": ", PATHSIZ - 1 - length);
		length += 2;
		strncat(messbuf, Errlist[errno], PATHSIZ - 1 - length);
	}
	/* to ensure '\0' termination of string */
	messbuf[PATHSIZ -1] = '\0';

	/* if message generated by a child of fmli, print it on line
	 * following cursor. if generated by fmli then use message line
	 */
	if (child == TRUE)		     	/* abs k15 */
	{
	    printf("\r\n%s\r\n", messbuf); 	/* abs k15 */
	    fflush(stdout);			/* abs k15 */
	}
	else
	{
	    mess_temp(messbuf);	
	    mess_flash(messbuf);		/* abs f15 */
	    doupdate();		       		/* abs f15 */
	}
}

#define LOGFILE		0
#define TERMINAL	1
#define MAILADM		2

/*
 * FACE application ONLY ....  log problems in the TERRLOG file
 * and/or send mail
 */
static void
log(sev, what, name, file, line)
int	sev;
int	what;
char	*name;
char	*file;
int	line;
{
	char	path[PATHSIZ];
	register int	method;
      	time_t	t;     /* EFT abs k16 */
	register FILE	*fp;
	extern char	*Oasys;
	extern char	*Progname;
	extern char	*sys_errlist[];
	char	*getenv();
	time_t	time();   /* EFT abs k16 */

	/*
	 * construct path of error log file
	 */
	method = LOGFILE;
	if (name == NULL)
		name = nil;
	strcat(strcpy(path, Oasys), TERRLOG);
	if ((fp = fopen(path, "a")) == NULL && errno == EMFILE) {
		close(4);
		fp = fopen(path, "a");
	}
	if (fp == NULL)
		if ((fp = popen("mail $LOGNAME", "w")) == NULL) {
			method = TERMINAL;
			fp = stderr;
		}
		else
			method = MAILADM;
	(void) time(&t);
	setbuf(fp, NULL);
	fprintf(fp, "%16.16s %-8s %-14s %-14s %3d %s%-*s %-24s %s\n", ctime(&t),
		getenv("LOGNAME"), Progname, file, line,
		(sev & TERR_EXIT) ? "(FATAL)" : nil,
		(sev & TERR_EXIT) ? 17 : 24,
		What[what], sys_errlist[errno], name);
	if (method == LOGFILE)
		fclose(fp);
	else if (method == MAILADM)
		pclose(fp);
}
