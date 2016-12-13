/*= -*- c-basic-offset: 4; indent-tabs-mode: nil; -*-
 *
 * $Id: stats.c,v 1.34 2004/09/10 02:48:58 mbp Exp $
 * 
 * Copyright (C) 2000, 2001 by Martin Pool <mbp@samba.org>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <config.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#include <string.h>

#include "librsync.h"
#include "trace.h"
#include "snprintf.h"

/*
 * TODO: Other things to show in statistics:
 *
 * Number of input and output bytes.
 *
 * Number of times we blocked waiting for input or output.
 *
 * Number of blocks.
 */

int
rs_log_stats(rs_stats_t const *stats)
{
    char buf[1000];

    rs_format_stats(stats, buf, sizeof buf - 1);
    rs_log(RS_LOG_INFO|RS_LOG_NONAME, "%s", buf);
    return 0;
}



/**
 * \brief Return a human-readable representation of statistics.
 *
 * The string is truncated if it does not fit.  100 characters should
 * be sufficient space.
 *
 * \param stats Statistics from an encoding or decoding operation.
 *
 * \param buf Buffer to receive result.
 * \param size Size of buffer.
 * \return buf
 */
char *
rs_format_stats(rs_stats_t const * stats,
		char *buf, size_t size)
{
    char const *op = stats->op;
    int len;

    if (!op)
        op = "noop";
    len = snprintf(buf, size, "%s statistics: ", op);

    if (stats->lit_cmds) {
        len += snprintf(buf+len, size-len,
                        "literal[%d cmds, " PRINTF_FORMAT_U64 " bytes, " PRINTF_FORMAT_U64 " cmdbytes] ",
                        stats->lit_cmds,
                        PRINTF_CAST_U64(stats->lit_bytes),
                        PRINTF_CAST_U64(stats->lit_cmdbytes));
    }

    if (stats->sig_cmds) {
        len += snprintf(buf+len, size-len,
                        "in-place-signature[" PRINTF_FORMAT_U64 " cmds, " PRINTF_FORMAT_U64 " bytes] ",
                        PRINTF_CAST_U64(stats->sig_cmds),
                        PRINTF_CAST_U64(stats->sig_bytes));
    }

    if (stats->copy_cmds || stats->false_matches) {
        len += snprintf(buf+len, size-len, 
                        "copy[" PRINTF_FORMAT_U64 " cmds, " PRINTF_FORMAT_U64 " bytes, " PRINTF_FORMAT_U64 " false, " PRINTF_FORMAT_U64 " cmdbytes]",
                        PRINTF_CAST_U64(stats->copy_cmds),
                        PRINTF_CAST_U64(stats->copy_bytes),
                        PRINTF_CAST_U64(stats->false_matches),
                        PRINTF_CAST_U64(stats->copy_cmdbytes));
    }
        

    if (stats->sig_blocks) {
        len  += snprintf(buf+len, size-len,
                         "signature[" PRINTF_FORMAT_U64 " blocks, " PRINTF_FORMAT_U64 " bytes per block]",
                         PRINTF_CAST_U64(stats->sig_blocks),
                         PRINTF_CAST_U64(stats->block_len));
    }
    
    return buf;
}
