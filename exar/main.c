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
    EXAR_FLAG_V = 1<<0,
    EXAR_FLAG_P = 1<<3,
    EXAR_FLAG_U = 1<<4,
    EXAR_FLAG_E = 1<<6,
    EXAR_FLAG_D = 1<<7,
    EXAR_FLAG_I = 1<<8,
    EXAR_FLAG_S = 1<<9,
};
#ifndef MIN
#define MIN(X, Y) ((X) > (Y) ? (Y) : (X))
#endif
#ifndef MAX
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#endif


#define EXAR_OPTION_FLAG (0xffff & ~(0x7))
#define EXAR_CHECK_FLAG(x, flag) !!(((x) & (flag)) && !((x) & ( (EXAR_OPTION_FLAG)^(flag) ) ))
void 
help(int ret)
{
    printf("USAGE: \n"
            "   exar option [arguments]\n\n" 
           "OPTIONS:\n" 
           "    h                   Print this help and exit.\n"
           "    d[v] archive file   Deletes a file from an archive, the file path is the\n"
           "                        relative file path of the file in the archive\n"
           "    e[v] archive file   Extracts a file from an archive and writes the content\n" 
           "                        to stdout, the archive is not modified, the file path \n"
           "                        is the relative file path of the file in the archive.\n"
           "    i[v] archive        Prints info about an archive\n"
           "    p[v] path           Pack file or directory 'path'.\n"
           "    s[v] archive file   Search for a file and write the content to stdout, the \n" 
           "                        archive is not modified, the filename is the basename\n" 
           "                        plus suffix of the file in the archive, all directory\n"
           "                        parts are stripped\n"
           "    u[v] file [dir]     Pack 'file' to directory 'dir' or to current directory.\n"
           "    v                   Verbose, pass multiple times (up to 3) to \n"
           "                        get more verbose messages.\n\n"
           "EXAMPLES:\n"
           "    exar p /tmp/foo          -- pack /tmp/foo to foo.exar\n"
           "    exar s foo.js > foo.js   -- Extract foo.js from the archive\n"
           "    exar uvvv foo.exar       -- unpack foo.exar to current directory,\n" 
           "                                verbosity level 3\n"
           "    exar vu foo.exar /tmp    -- unpack foo.exar to /tmp, verbosity\n" 
           "                                level 1\n");
    exit(ret);
}
static void 
extract(const char *archive, const char *path, 
        unsigned char * (*extract_func)(const char *, const char *, size_t *))
{
    size_t s;
    unsigned char *content = extract_func(archive, path, &s);
    if (content != NULL)
    {
        fwrite(content, 1, s, stdout);
        free(content);
    }
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
                flag |= EXAR_FLAG_P;
                break;
            case 'u' : 
                flag |= EXAR_FLAG_U;
                break;
            case 'e' : 
                flag |= EXAR_FLAG_E;
                break;
            case 'd' : 
                flag |= EXAR_FLAG_D;
                break;
            case 'i' : 
                flag |= EXAR_FLAG_I;
                break;
            case 's' : 
                flag |= EXAR_FLAG_S;
                break;
            case 'v' : 
                flag |= MAX(EXAR_FLAG_V, MIN(EXAR_VERBOSE_MASK, ((flag & EXAR_VERBOSE_MASK) << 1)));
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

    if (EXAR_CHECK_FLAG(flag, EXAR_FLAG_U))
        exar_unpack(argv[2], argv[3]);
    else if (EXAR_CHECK_FLAG(flag, EXAR_FLAG_P))
        exar_pack(argv[2]);
    else if (EXAR_CHECK_FLAG(flag, EXAR_FLAG_I))
        exar_info(argv[2]);
    else if (EXAR_CHECK_FLAG(flag, EXAR_FLAG_D) && argc > 3)
        exar_delete(argv[2], argv[3]);
    else if (EXAR_CHECK_FLAG(flag, EXAR_FLAG_E) && argc > 3)
        extract(argv[2], argv[3], exar_extract);
    else if (EXAR_CHECK_FLAG(flag, EXAR_FLAG_S) && argc > 3)
        extract(argv[2], argv[3], exar_search_extract);
    else 
        help(EXIT_FAILURE);

    return EXIT_SUCCESS;
}
