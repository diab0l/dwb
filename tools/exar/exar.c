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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include <ftw.h>
#include "exar.h"

#define VERSION_BASE "exar-"
#define VERSION VERSION_BASE "1"
#define EXTENSION "exar"

#define SZ_VERSION 8
#define SZ_DFLAG  1
#define SZ_NAME  115
#define SZ_SIZE  12 

#define HDR_NAME (0)
#define HDR_DFLAG (HDR_NAME + SZ_NAME)
#define HDR_SIZE (HDR_DFLAG + SZ_DFLAG)
#define HDR_END (HDR_SIZE + SZ_SIZE)

#define DIR_FLAG    (100)
#define FILE_FLAG  (102)

#define LOG(level, ...) do { if (s_verbose & EXAR_VERBOSE_L##level) \
    fprintf(stderr, "exar-log%d: ", level); \
    fprintf(stderr, __VA_ARGS__); } while(0)

static size_t s_offset;
static FILE *s_out;
static unsigned char s_verbose = 0;

static int
pack(const char *fpath, const struct stat *st, int tf)
{
    (void)tf;

    char buffer[HDR_END] = {0};
    char rbuf[32];
    size_t r;
    FILE *f = NULL;
    const char *stripped = &fpath[s_offset];

    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer + HDR_NAME, stripped, SZ_NAME);

    LOG(1, "Packing %s (archive path: %s)\n", fpath, stripped);
    if (S_ISDIR(st->st_mode))
        buffer[HDR_DFLAG] = DIR_FLAG;
    else if (S_ISREG(st->st_mode))
    {
        buffer[HDR_DFLAG] = FILE_FLAG;
        LOG(3, "Opening %s for reading\n", fpath);
        f = fopen(fpath, "r");
        if (f == NULL)
        {
            perror("fopen");
            return 0;
        }
    }
    else 
    {
        LOG(1, "Only directories and regular files will be packed, ignoring %s\n", fpath);
        return 0;
    }

    LOG(2, "Writing file header for %s\n", fpath);
    snprintf(buffer + HDR_SIZE, SZ_SIZE, "%.11o", (unsigned int)st->st_size);
    fwrite(buffer, 1, sizeof(buffer), s_out);

    if (f != NULL)
    {
        LOG(2, "Writing %s (%lu bytes)\n", stripped, (st->st_size));
        while ((r = fread(rbuf, 1, sizeof(rbuf), f)) > 0)
            fwrite(rbuf, 1, r, s_out);

        LOG(3, "Closing %s\n", fpath);
        fclose(f);
    }
    return 0;
}

size_t 
get_offset(char *buffer, size_t n, const char *path, int *end)
{
    const char *tmp = path, *slash;
    size_t len = strlen(path);
    size_t offset = 0;
    int i=0;

    // strip trailing '/'
    while (tmp[len-1] == '/')
        len--;
    strncpy(buffer, path, len);

    // get base name offset
    slash = strrchr(buffer, '/');
    if (slash != NULL)
        offset = slash - buffer + 1;
    for (tmp = path + s_offset; *tmp && *tmp != '/'; i++, tmp++)
        buffer[i] = *tmp;
    if (end != NULL)
        *end = i;
    return offset;
}


int 
exar_pack(const char *path)
{
    int ret;
    unsigned char version[SZ_VERSION] = {0};
    char buffer[512];
    int i = 0;

    s_offset = get_offset(buffer, sizeof(buffer), path, &i);

    strncpy(&buffer[i], "." EXTENSION, sizeof(buffer) - i);

    LOG(3, "Opening %s for writing\n", buffer);
    if ((s_out = fopen(buffer, "w")) == NULL)
    {
        perror("fopen");
        return -1;
    }

    // set version header
    LOG(2, "Writing version header (%s)\n", VERSION);
    memcpy(version, VERSION, sizeof(version));
    fwrite(version, 1, sizeof(version), s_out);

    ret = ftw(path, pack, 64);

    LOG(3, "Closing %s\n", buffer);
    fclose(s_out);
    return ret;
}
static int 
check_version(FILE *f, int verbose)
{
    unsigned char version[SZ_VERSION] = {0}, orig_version[SZ_VERSION] = {0};
    LOG(2, "Reading version header\n");
    if (fread(version, 1, sizeof(version), f) != sizeof(version))
    {
        if (verbose)
            fprintf(stderr, "Not an exar file?\n");
        return -1;
    }
    memcpy(orig_version, VERSION, sizeof(orig_version));
    LOG(2, "Checking filetype\n");
    if (strncmp((char*)version, VERSION_BASE, 5))
    {
        if (verbose)
            fprintf(stderr, "Not an exar file?\n");
        return -1;
    }

    LOG(1, "Found version %s\n", version);
    if (memcmp(version, orig_version, SZ_VERSION))
    {
        if (verbose)
            fprintf(stderr, "Incompatible version number\n");
        return -1;
    }
    return 0;
}
int 
exar_unpack(const char *path, const char *dest)
{
    int ret = -1;
    char name[SZ_NAME], size[SZ_SIZE], flag, rbuf;
    size_t fs;
    FILE *of, *f = NULL;
    char *endptr;

    LOG(3, "Opening %s for reading\n", path);
    if ((f = fopen(path, "r")) == NULL)
    {
        fprintf(stderr, "Cannot open %s\n", path);
        return -1;
    }
    if (check_version(f, 1) != 0)
        goto error_out;

    if (dest != NULL)
    {
        LOG(2, "Changing to directory %s\n", dest);
        if (chdir(dest) != 0)
        {
            perror("chdir");
            goto error_out;
        }
    }

    while (1) 
    {
        if (fread(name, 1, SZ_NAME, f) != SZ_NAME)
            break;
        if (fread(&flag, 1, SZ_DFLAG, f) != SZ_DFLAG)
            break;
        if (fread(size, 1, SZ_SIZE, f) != SZ_SIZE)
            break;
        name[SZ_NAME-1] = 0;
        size[SZ_SIZE-1] = 0;
        if (flag != DIR_FLAG && flag != FILE_FLAG)
        {
            LOG(1, "No file flag found for %s\n", name);
            fprintf(stderr, "The archive seems to be corrupted%s", "\n");
            goto error_out;
        }
        if (flag == DIR_FLAG) 
        {
            LOG(1, "Creating directory %s\n", name);
            mkdir(name, 0755);
        }
        else 
        {
            LOG(1, "Unpacking %s\n", name);

            fs = strtoul(size, &endptr, 8);
            if (*endptr)
            {
                LOG(1, "Cannot determine file size for %s\n", name);
                fprintf(stderr, "The archive seems to be corrupted%s", "\n");
                goto error_out;
            }

            LOG(3, "Opening %s for writing\n", name);
            of = fopen(name, "w");
            if (of == NULL)
            {
                perror("fopen");
                goto error_out;
            }

            LOG(2, "Writing %s (%lu bytes)\n", name, fs);
            for (size_t i=0; i<fs; i++)
            {
                if (fread(&rbuf, 1, 1, f) != 0)
                    fwrite(&rbuf, 1, 1, of);
            }
            LOG(3, "Closing %s\n", name);
            fclose(of);
        }
    }
    ret = 0;
error_out:
    if (f != NULL)
    {
        LOG(3, "Closing %s\n", path);
        fclose(f);
    }
    return ret;
}
int 
exar_cat(const char *file1, const char *file2)
{
    int ret = -1;
    size_t r;
    FILE *f1 = NULL, *f2 = NULL;
    char buffer[64];
    char offset_buffer[512];

    LOG(3, "Opening file %s for writing\n", file1);
    if ((f1 = fopen(file1, "a")) == NULL)
    {
        perror("fopen");
        goto error_out;
    }
    LOG(3, "Opening file %s for reading\n", file2);
    if ((f2 = fopen(file2, "r")) == NULL)
    {
        perror("fopen");
        goto error_out;
    }
    if (check_version(f2, 0) == 0)
    {
        LOG(1, "Concatenating archive\n");
        while ((r = fread(buffer, 1, sizeof(buffer), f2)) > 0)
        {
            fwrite(buffer, 1, r, f1);
        }
    }
    else 
    {
        s_offset = get_offset(offset_buffer, sizeof(offset_buffer), file2, NULL);
        LOG(1, "Concatenating regular files\n");
        s_out = f1;
        ftw(file2, pack, 64);
    }
    ret = 0;
error_out: 
    if (f1 != NULL)
    {
        LOG(3, "Closing %s\n", file1);
        fclose(f1);
    }
    if (f2 != NULL)
    {
        LOG(3, "Closing %s\n", file2);
        fclose(f2);
    }
    return ret;
}
void 
exar_verbose(unsigned char v)
{
    s_verbose = v & EXAR_VERBOSE_MASK;
}
