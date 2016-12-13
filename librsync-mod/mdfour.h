/*= -*- c-basic-offset: 4; indent-tabs-mode: nil; -*-
 *
 * librsync -- the library for network deltas
 * $Id: mdfour.h,v 1.7 2003/10/17 16:15:21 abo Exp $
 * 
 * Copyright (C) 2000, 2001 by Martin Pool <mbp@samba.org>
 * Copyright (C) 2002, 2003 by Donovan Baarda <abo@minkirri.apana.org.au> 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "types.h"

#define uchar unsigned char
#define uint32 uint32_t
#define MD4_DIGEST_LEN 16
#define MD5_DIGEST_LEN 16
#define MAX_DIGEST_LEN MD5_DIGEST_LEN
#define CSUM_CHUNK 64
#define IVAL(buf,pos) (*(uint32 *)((char *)(buf) + (pos)))
#define SIVAL(buf,pos,val) IVAL(buf,pos)=((uint32)(val))
#define IVALu(buf,pos) IVAL(buf,pos)
#define SIVALu(buf,pos,val) SIVAL(buf,pos,val)


typedef struct MD_context_tmp {
	uint32 A, B, C, D;
	uint32 totalN;          /* bit count, lower 32 bits */
	uint32 totalN2;         /* bit count, upper 32 bits */
	uchar buffer[CSUM_CHUNK];
} md_context;


/*struct rs_mdfour {
    int                 A, B, C, D;
#if HAVE_UINT64
    uint64_t            totalN;
#else
    uint32_t            totalN_hi, totalN_lo;
#endif
    int                 tail_len;
    unsigned char       tail[64];
};*/
