/*
 * Copyright (c) 2012-2013 Stefan Bolte <portix@gmx.net>
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
#include "lexar.h"

enum {
    FLAG_V = 1<<0,
    FLAG_P = 1<<3,
    FLAG_U = 1<<4,
};
#ifndef MIN
#define MIN(X, Y) ((X) > (Y) ? (Y) : (X))
#endif
#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif
void 
help(int ret)
{
    printf("USAGE: \n"
            "   exar option [arguments]\n\n" 
           "OPTIONS:\n" 
           "    h                : Print this help and exit.\n"
           "    p[v] path        : Pack file or directory 'path'.\n"
           "    u[v] file [dir]  : Pack 'file' to directory 'dir' or to \n" 
           "                       current directory.\n"
           "    v                : Verbose, pass multiple times (up to 3) to \n"
           "                       get more verbose messages.\n\n"
           "EXAMPLES:\n"
           "    exar p /tmp/foo        -- pack /tmp/foo to foo.exar\n"
           "    exar uvvv foo.exar     -- unpack foo.exar to current directory, \n" 
           "                              verbosity level 3\n"
           "    exar vu foo.exar /tmp  -- unpack foo.exar to /tmp, verbosity \n" 
           "                              level 1\n");
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
    if ((flag & (FLAG_U | FLAG_P)) == (FLAG_U | FLAG_P))
        help(EXIT_FAILURE);
    if (flag & EXAR_VERBOSE_MASK)
        exar_verbose(flag);

    if (flag & FLAG_U)
        exar_unpack(argv[2], argv[3]);
    else if (flag & FLAG_P)
        exar_pack(argv[2]);
    else 
        help(EXIT_SUCCESS);

    return EXIT_SUCCESS;
}
