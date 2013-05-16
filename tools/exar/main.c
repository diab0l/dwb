/*
 * Copyright (c) 2013 Stefan Bolte <portix@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exar.h"

enum {
    FLAG_V = 1<<0,
    FLAG_P = 1<<3,
    FLAG_U = 1<<4,
    FLAG_C = 1<<5,
    FLAG_E = 1<<6,
};
#ifndef MIN
#define MIN(X, Y) ((X) > (Y) ? (Y) : (X))
#endif
#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif

#define OP_FLAG(flag) ((flag) & ((FLAG_P|FLAG_U|FLAG_C|FLAG_E)^(flag)))
#define CHECK_FLAG(x, flag) (((x) & (flag)) && !((x) & ( (FLAG_P|FLAG_U|FLAG_C|FLAG_E)^(flag) ) ))
void 
help(int ret)
{
    printf("USAGE: \n"
            "   exar option [arguments]\n\n" 
           "OPTIONS:\n" 
           "    h                 : Print this help and exit.\n"
           "    c[v] archive file : Concatenates a file, directory or archive to \n" 
           "                        an existing archive.\n"
           "    e[v] archive file : Extracts an file from an archive and writes the content\n" 
           "                        to stdout, the archive is not modified, the file path \n"
           "                        is the relative file path of the file in the archive.\n"
           "    p[v] path         : Pack file or directory 'path'.\n"
           "    u[v] file [dir]   : Pack 'file' to directory 'dir' or to \n" 
           "                        current directory.\n"
           "    v                 : Verbose, pass multiple times (up to 3) to \n"
           "                        get more verbose messages.\n\n"
           "EXAMPLES:\n"
           "    exar p /tmp/foo          -- pack /tmp/foo to foo.exar\n"
           "    exar c foo.exar bar.txt  -- Concatenates bar.txt to archive foo.exar\n"
           "    exar c foo.exar bar.exar -- Concatenates archive bar.exar to archive foo.exar\n"
           "    exar uvvv foo.exar       -- unpack foo.exar to current directory, \n" 
           "                                verbosity level 3\n"
           "    exar vu foo.exar /tmp    -- unpack foo.exar to /tmp, verbosity \n" 
           "                                level 1\n");
    exit(ret);
}
int 
main (int argc, char **argv)
{
    int flag = 0;
    if (argc < 3)
    {
        help(EXIT_FAILURE);
    }
    const char *options = argv[1];
    while (*options)
    {
        switch (*options) 
        {
            case 'p' : 
                flag |= FLAG_P;
                break;
            case 'u' : 
                flag |= FLAG_U;
                break;
            case 'c' : 
                flag |= FLAG_C;
                break;
            case 'e' : 
                flag |= FLAG_E;
                break;
            case 'v' : 
                flag |= MAX(FLAG_V, MIN(EXAR_VERBOSE_MASK, ((flag & EXAR_VERBOSE_MASK) << 1)));
                break;
            case 'h' : 
                help(EXIT_SUCCESS);
            default : 
                help(EXIT_FAILURE);
        }
        options++;
    }
    if (flag & EXAR_VERBOSE_MASK)
        exar_verbose(flag);

    if (CHECK_FLAG(flag, FLAG_U))
        exar_unpack(argv[2], argv[3]);
    else if (CHECK_FLAG(flag, FLAG_P))
        exar_pack(argv[2]);
    else if (CHECK_FLAG(flag, FLAG_C) && argc > 3)
        exar_cat(argv[2], argv[3]);
    else if (CHECK_FLAG(flag, FLAG_E) && argc > 3)
    {
        size_t s;
        unsigned char *content = exar_extract(argv[2], argv[3], &s);
        if (content != NULL)
        {
            fwrite(content, 1, s, stdout);
            free(content);
        }
    }
    else 
        help(EXIT_FAILURE);

    return EXIT_SUCCESS;
}
