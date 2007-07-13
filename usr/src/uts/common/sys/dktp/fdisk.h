/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*	Copyright (c) 1984, 1986, 1987, 1988 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef _SYS_DKTP_FDISK_H
#define	_SYS_DKTP_FDISK_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * fdisk.h
 * This file defines the structure of physical disk sector 0 for use on
 * AT386 systems.  The format of this sector is constrained by the ROM
 * BIOS and MS-DOS conventions.
 * Note that this block does not define the partitions used by the unix
 * driver.  The unix partitions are obtained from the VTOC.
 */

/*
 * BOOTSZ was reduced from 446 to 440 bytes to NOT overwrite the Windows
 * Vista DISKID. Otherwise Vista won't boot from Solaris GRUB in a dual-boot
 * setup.
 * The actual size of mboot code is 425 bytes while that of GRUB stage1 is
 * 423 bytes. So this changes does not harm them.
 */
#define	BOOTSZ		440	/* size of boot code in master boot block */
#define	FD_NUMPART	4	/* number of 'partitions' in fdisk table */
#define	MBB_MAGIC	0xAA55	/* magic number for mboot.signature */
#define	DEFAULT_INTLV	4	/* default interleave for testing tracks */
#define	MINPSIZE	4	/* minimum number of cylinders in a partition */
#define	TSTPAT		0xE5	/* test pattern for verifying disk */

/*
 * structure to hold the fdisk partition table
 */
struct ipart {
	unsigned char bootid;	/* bootable or not */
	unsigned char beghead;	/* beginning head, sector, cylinder */
	unsigned char begsect;	/* begcyl is a 10-bit number. High 2 bits */
	unsigned char begcyl;	/*	are in begsect. */
	unsigned char systid;	/* OS type */
	unsigned char endhead;	/* ending head, sector, cylinder */
	unsigned char endsect;	/* endcyl is a 10-bit number.  High 2 bits */
	unsigned char endcyl;	/*	are in endsect. */
	int	relsect;	/* first sector relative to start of disk */
	int	numsect;	/* number of sectors in partition */
};
/*
 * Values for bootid.
 */
#define	NOTACTIVE	0
#define	ACTIVE		128
/*
 * Values for systid.
 */
#define	DOSOS12		1	/* DOS partition, 12-bit FAT */
#define	PCIXOS		2	/* PC/IX partition */
#define	DOSOS16		4	/* DOS partition, 16-bit FAT */
#define	EXTDOS		5	/* EXT-DOS partition */
#define	DOSHUGE		6	/* Huge DOS partition  > 32MB */
#define	FDISK_IFS	7	/* Installable File System (IFS): HPFS & NTFS */
#define	FDISK_AIXBOOT	8	/* AIX Boot */
#define	FDISK_AIXDATA	9	/* AIX Data */
#define	FDISK_OS2BOOT	10	/* OS/2 Boot Manager */
#define	FDISK_WINDOWS	11	/* Windows 95 FAT32 (up to 2047GB) */
#define	FDISK_EXT_WIN	12	/* Windows 95 FAT32 (extended-INT13) */
#define	FDISK_FAT95	14	/* DOS 16-bit FAT, LBA-mapped */
#define	FDISK_EXTLBA	15	/* Extended partition, LBA-mapped */
#define	DIAGPART	18	/* Diagnostic boot partition (OS independent) */
#define	FDISK_LINUX	65	/* Linux */
#define	FDISK_LINUXDSWAP	66	/* Linux swap (sharing disk w/ DRDOS) */
#define	FDISK_LINUXDNAT	67	/* Linux native (sharing disk with DRDOS) */
#define	FDISK_CPM	82	/* CP/M */
#define	DOSDATA		86	/* DOS data partition */
#define	OTHEROS		98	/* part. type for appl. (DB?) needs */
				/* raw partition.  ID was 0 but conflicted */
				/* with DOS 3.3 fdisk    */
#define	UNIXOS		99	/* UNIX V.x partition */
#define	UNUSED		100	/* unassigned partition */
#define	FDISK_NOVELL3	101	/* Novell Netware 3.x and later */
#define	FDISK_QNX4	119	/* QNX 4.x */
#define	FDISK_QNX42	120	/* QNX 4.x 2nd part */
#define	FDISK_QNX43	121	/* QNX 4.x 3rd part */
#define	SUNIXOS		130	/* Solaris UNIX partition */
#define	FDISK_LINUXNAT	131	/* Linux native */
#define	FDISK_NTFSVOL1	134	/* NTFS volume set 1 */
#define	FDISK_NTFSVOL2	135	/* NTFS volume set 2 */
#define	FDISK_BSD	165	/* BSD/386, 386BSD, NetBSD, FreeBSD, OpenBSD */
#define	FDISK_NEXTSTEP	167	/* NeXTSTEP */
#define	FDISK_BSDIFS	183	/* BSDI file system */
#define	FDISK_BSDISWAP	184	/* BSDI swap */
#define	X86BOOT		190	/* x86 Solaris boot partition */
#define	SUNIXOS2	191	/* Solaris UNIX partition */
#define	EFI_PMBR	238	/* EFI PMBR */
#define	EFI_FS		239	/* EFI File System (System Partition) */
#define	MAXDOS		65535L	/* max size (sectors) for DOS partition */

/*
 * structure to hold master boot block in physical sector 0 of the disk.
 * Note that partitions stuff can't be directly included in the structure
 * because of lameo '386 compiler alignment design.
 */

struct mboot {	/* master boot block */
	char     bootinst[BOOTSZ];
	uint32_t win_volserno;
	uint16_t reserved;
	char     parts[FD_NUMPART * sizeof (struct ipart)];
	ushort_t signature;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DKTP_FDISK_H */
