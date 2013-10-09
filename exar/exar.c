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

#ifndef _BSD_SOURCE
#define _BSD_SOURCE 
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include <ftw.h>
#include <errno.h>
#include <assert.h>
#include <inttypes.h>
#include "exar.h"

#define EXAR_VERSION_BASE "exar-"
#define EXAR_VERSION EXAR_VERSION_BASE "1"
#define EXTENSION "exar"

#define SZ_VERSION 7
#define SZ_DFLAG  1
#define SZ_SIZE  14 

#define HDR_DFLAG (0)
#define HDR_SIZE (HDR_DFLAG + SZ_DFLAG)
#define HDR_NAME (HDR_SIZE + SZ_SIZE)

#define DIR_FLAG    (100)
#define FILE_FLAG  (102)

#define MAX_FILE_HANDLES 64

#define MIN(X, Y)  ((X) > (Y) ? (Y) : (X))

#define EXAR_NAME_MAX 4096

#define EE_OK     0
#define EE_ERROR -1
#define EE_EOF   -2

struct exar_header_s {
    unsigned char eh_flag;
    off_t eh_size;
    char eh_name[EXAR_NAME_MAX];
};
#define EXAR_HEADER_EMPTY  { 0, 0, { 0 } }  

#define LOG(level, ...) do { if (s_verbose & EXAR_VERBOSE_L##level) { \
    fprintf(stderr, "exar-log%d: ", level); \
    fprintf(stderr, __VA_ARGS__); } } while(0)

static size_t s_offset;
static FILE *s_out;
static unsigned char s_verbose = 0;
static const char *s_out_path;

static void *
xcalloc(size_t nmemb, size_t size)
{
    void *ret = calloc(nmemb, size);
    if (ret == NULL)
    {
        fprintf(stderr, "Cannot malloc %zu bytes\n", size * nmemb);
        exit(EXIT_FAILURE);
    }
    return ret;
}

static size_t 
get_offset(char *buffer, size_t n, const char *path, int *end)
{
    const char *tmp = path, *slash;
    size_t len = strlen(path);
    size_t offset = 0;
    int i=0;

    // strip trailing '/'
    while (tmp[len-1] == '/')
        len--;
    strncpy(buffer, path, MIN(n, len));

    // get base name offset
    slash = strrchr(buffer, '/');
    if (slash != NULL)
        offset = slash - buffer + 1;
    for (tmp = path + offset; *tmp && *tmp != '/'; i++, tmp++)
        buffer[i] = *tmp;
    if (end != NULL)
        *end = i;
    return offset;
}
static int 
check_version(FILE *f, int verbose)
{
    unsigned char version[SZ_VERSION] = {0};
    unsigned char orig_version[SZ_VERSION] = {0};
    LOG(2, "Reading version header\n");
    if (fread(version, 1, SZ_VERSION, f) != SZ_VERSION)
    {
        if (feof(f))
            return EE_EOF;
        else 
        {
            if (verbose)
                fprintf(stderr, "Not an exar file?\n");
            return EE_ERROR;
        }
    }
    memcpy(orig_version, EXAR_VERSION, sizeof(orig_version));
    LOG(2, "Checking filetype\n");
    if (strncmp((char*)version, EXAR_VERSION_BASE, 5))
    {
        if (verbose)
            fprintf(stderr, "Not an exar file?\n");
        return EE_ERROR;
    }

    LOG(2, "Found version %s\n", version);
    if (memcmp(version, orig_version, SZ_VERSION))
    {
        if (verbose)
            fprintf(stderr, "Incompatible version number\n");
        return EE_ERROR;
    }
    return EE_OK;
}
/*
 * Opens archive and checks version, mode is either read or read-write
 * */ 
static FILE * 
open_archive(const char *path, const char *mode)
{
    FILE *f = NULL;
    LOG(3, "Opening %s for %s\n", path, strcmp(mode, "r") == 0 ? "reading" : "reading and writing");
    if ((f = fopen(path, mode)) == NULL)
    {
        perror(path);
        return NULL;
    }
    return f;
}
static void 
close_file(FILE *f, const char *archive)
{
    if (f != NULL)
    {
        LOG(3, "Closing %s\n", archive);
        fclose(f);
    }
}

static int 
get_file_header(FILE *f, struct exar_header_s *head)
{
    char *endptr;
    char header[HDR_NAME];
    off_t fs;
    char rb;
    size_t i = 0;
    int st_version = 0;

    if ((st_version = check_version(f, 1)) != EE_OK)
        return st_version;

    LOG(2, "Reading file header\n");
    if (fread(header, 1, HDR_NAME, f) != HDR_NAME)
    {
        fprintf(stderr, "Reading file header failed");
        return EE_ERROR;
    }

    head->eh_flag = header[HDR_DFLAG];

    if (head->eh_flag != DIR_FLAG && head->eh_flag != FILE_FLAG)
    {
        LOG(1, "No file flag found\n");
        fprintf(stderr, "The archive seems to be corrupted\n");
        return EE_ERROR;
    }
    if (head->eh_flag == FILE_FLAG)
    {
        fs = strtol(&header[HDR_SIZE], &endptr, 16);
        if (*endptr)
        {
            LOG(1, "Cannot determine file size\n");
            fprintf(stderr, "The archive seems to be corrupted\n");
            return EE_ERROR;
        }
        head->eh_size = fs;
    }
    else 
        head->eh_size = 0;

    while (fread(&rb, 1, 1, f) > 0)
    {
        head->eh_name[i] = rb;
        i++;
        if (rb == '\0')
            break;
        else if (i == EXAR_NAME_MAX)
        {
            fprintf(stderr, "Cannot get filename\n");
            return EE_ERROR;
        }
    }

    LOG(2, "Found file header (%s, %c, %jd)\n", head->eh_name, head->eh_flag, (intmax_t)head->eh_size);
    return EE_OK;
}
static int 
next_file(FILE *f, struct exar_header_s *header)
{
    if (*(header->eh_name))
    {
        if (header->eh_flag == FILE_FLAG)
        {
            if (fseek(f, header->eh_size, SEEK_CUR) != 0)
                return EE_ERROR;
            else 
                LOG(3, "Skipping %s\n", header->eh_name);
        }
    }
    return get_file_header(f, header);
}
static int 
find_cmp(const char *name, const char *search)
{
    char buffer[EXAR_NAME_MAX];
    if (strcmp(name, search) != 0)
    {
        size_t offset = get_offset(buffer, EXAR_NAME_MAX, name, NULL);
        return strcmp(&name[offset], search);
    }
    return 0;
}
static int 
contains(const char *archive, const char *name, int (*cmp)(const char *, const char *))
{
    FILE *f = NULL;
    struct exar_header_s header = EXAR_HEADER_EMPTY;
    int result = EE_ERROR;

    if ((f = open_archive(archive, "r")) == NULL)
        goto finish;
    while (next_file(f, &header) == EE_OK)
    {
        if (cmp(header.eh_name, name) == 0)
        {
            result = EE_OK;
            break;
        }
    }

finish:
    close_file(f, archive);
    return result;
}
static unsigned char *
extract(const char *archive, const char *file, off_t *s, int (*cmp)(const char *, const char *))
{
    struct exar_header_s header = EXAR_HEADER_EMPTY;
    FILE *f = NULL;
    unsigned char *ret = NULL;
    if (s != NULL)
        *s = 0;

    if ((f = open_archive(archive, "r")) == NULL)
        goto finish;
    while (get_file_header(f, &header) == EE_OK)
    {
        if (cmp(header.eh_name, file) == 0)
        {
            if (header.eh_flag == FILE_FLAG)
            {
                ret = xcalloc(header.eh_size, sizeof(unsigned char));
                LOG(3, "Reading %s\n", header.eh_name);
                if (fread(ret, 1, header.eh_size, f) != (size_t)header.eh_size)
                {
                    fprintf(stderr, "Failed to read %s\n", header.eh_name);
                    *s = -1;
                    free(ret);
                    ret = NULL;
                }
                else if (s != NULL)
                    *s = header.eh_size;
            }
            else 
                fprintf(stderr, "%s is a directory, only regular files can be extracted\n", file);
            goto finish;
        }
        else if (header.eh_flag == FILE_FLAG)
        {
            LOG(3, "Skipping %s\n", header.eh_name);
            fseek(f, header.eh_size, SEEK_CUR);
        }
    }
    fprintf(stderr, "File %s was not found in %s\n", file, archive);
finish:
    close_file(f, archive);
    return ret;
}

static int 
write_file_header(FILE *f, const char *name, char flag, off_t r)
{
    unsigned char version[SZ_VERSION] = {0};
    char buffer[HDR_NAME] = {0};
    size_t l_name; 
    char term = 0;

    l_name = strlen(name);
    if (l_name > EXAR_NAME_MAX)
    {
        fprintf(stderr, "Filename too long\n");
        return EE_ERROR;
    }

    LOG(2, "Writing version header (%s)\n", EXAR_VERSION);

    memcpy(version, EXAR_VERSION, sizeof(version));
    if (fwrite(version, 1, sizeof(version), f) != sizeof(version))
    {
        fprintf(stderr, "Failed to write %zu bytes", sizeof(version));
        return EE_ERROR;
    }

    LOG(2, "Writing file header for %s\n", name);

    memset(buffer, 0, sizeof(buffer));
    buffer[HDR_DFLAG] = flag;
    snprintf(buffer + HDR_SIZE, SZ_SIZE, "%.13x", flag == FILE_FLAG ? (unsigned int)r : 0);
    if (fwrite(buffer, 1, HDR_NAME, f) != HDR_NAME)
        return EE_ERROR;
    if (fwrite(name, 1, l_name, f) != l_name)
        return EE_ERROR;
    if (fwrite(&term, 1, 1, f) != 1)
        return EE_ERROR;

    return EE_OK;
}

static int
ftw_pack(const char *fpath, const struct stat *st, int tf)
{
    (void)tf;

    int result = -1;
    unsigned char rbuf[512];
    size_t r;
    FILE *f = NULL;
    char flag;
    const char *stripped = &fpath[s_offset];

    if (!strcmp(stripped, s_out_path))
    {
        LOG(3, "Skipping output file %s\n", s_out_path);
        return 0;
    }
    LOG(1, "Packing %s (archive path: %s)\n", fpath, stripped);
    if (S_ISDIR(st->st_mode))
    {
        if (!strcmp(fpath, "."))
        {
            s_offset = 2;
            LOG(3, "Skipping directory .\n");
            return 0;
        }
        flag = DIR_FLAG;
    }
    else if (S_ISREG(st->st_mode))
    {
        flag = FILE_FLAG;
        LOG(3, "Opening %s for reading\n", fpath);
        f = fopen(fpath, "r");
        if (f == NULL)
        {
            perror(fpath);
            return 0;
        }
    }
    else 
    {
        LOG(1, "Only directories and regular files will be packed, ignoring %s\n", fpath);
        return 0;
    }

    if (write_file_header(s_out, stripped, flag, st->st_size) != 0) 
        goto finish;

    if (f != NULL)
    {
        LOG(2, "Writing %s (%jd bytes)\n", stripped, (intmax_t)(st->st_size));
        while ((r = fread(rbuf, 1, sizeof(rbuf), f)) > 0)
        {
            if (fwrite(rbuf, 1, r, s_out) != r)
            {
                fprintf(stderr, "Failed to write %zu bytes", r);
                goto finish;
            }
        }
    }
    result = 0;
finish: 
    close_file(f, fpath);
    return result;
}
static int 
pack (const char *archive, const char *path, const char *mode)
{
    int ret = EE_OK;

    LOG(3, "Opening %s for writing\n", archive);
    if ((s_out = fopen(archive, mode)) == NULL)
    {
        perror(archive);
        return EE_ERROR;
    }
    s_out_path = archive;

    ret = ftw(path, ftw_pack, MAX_FILE_HANDLES);

    LOG(3, "Closing %s\n", archive);

    fclose(s_out);
    return ret;
}

int 
exar_pack(const char *path, const char *outpath)
{
    assert(path != NULL);

    char archive[EXAR_NAME_MAX];
    int i = 0;

    s_offset = get_offset(archive, sizeof(archive), path, &i);

    if (outpath != NULL)
    {
        strncpy(archive, outpath, sizeof(archive));
    }
    else 
    {
        strncpy(&archive[i], "." EXTENSION, sizeof(archive) - i);
    }

    return pack(archive, path, "w");
}
int 
exar_append(const char *archive, const char *path)
{
    assert(path != NULL);
    char stripped[EXAR_NAME_MAX];

    s_offset = get_offset(stripped, sizeof(stripped), path, 0);

    return pack(archive, path, "a");
}

int 
exar_unpack(const char *archive, const char *dest)
{
    assert(archive != NULL);

    struct exar_header_s header = EXAR_HEADER_EMPTY;
    int ret = EE_ERROR;
    FILE *of, *f = NULL;
    unsigned char buf[512];
    size_t r = 0;
    int status;

    if ((f = open_archive(archive, "r")) == NULL)
        goto finish;

    if (dest != NULL)
    {
        LOG(2, "Changing to directory %s\n", dest);
        if (chdir(dest) != 0)
        {
            perror(dest);
            goto finish;
        }
    }

    while (1) 
    {
        status = get_file_header(f, &header);
        if (status == EE_EOF)
            break;
        else if (status == EE_ERROR)
            goto finish;

        if (header.eh_flag == DIR_FLAG) 
        {
            LOG(1, "Creating directory %s\n", header.eh_name);
            if (mkdir(header.eh_name, 0755) != 0 && errno != EEXIST) 
            {
                perror(header.eh_name);
                goto finish;
            }
        }
        else 
        {
            LOG(1, "Unpacking %s\n", header.eh_name);

            LOG(3, "Opening %s for writing\n", header.eh_name);
            of = fopen(header.eh_name, "w");
            if (of == NULL)
            {
                perror(header.eh_name);
                goto finish;
            }

            LOG(2, "Writing %s (%jd bytes)\n", header.eh_name, (intmax_t)header.eh_size);
            for (off_t i=0; i<header.eh_size; i += sizeof(buf))
            {
                if ( (r = fread(buf, 1, MIN(sizeof(buf), (size_t)header.eh_size - i), f)) != 0)
                {
                    if (fwrite(buf, 1, r, of) != r)
                    {
                        fprintf(stderr, "Failed to write %zu bytes\n", r);
                        goto finish;
                    }
                }
            }
            LOG(3, "Closing %s\n", header.eh_name);
            fclose(of);
        }
    }
    ret = EE_OK;
finish:
    close_file(f, archive);
    return ret;
}

unsigned char * 
exar_extract(const char *archive, const char *file, off_t *s)
{
    assert(archive != NULL && file != NULL);

    return extract(archive, file, s, strcmp);
}
unsigned char * 
exar_search_extract(const char *archive, const char *file, off_t *s)
{
    assert(archive != NULL && file != NULL);

    return extract(archive, file, s, find_cmp);
}
int 
exar_delete(const char *archive, const char *file)
{
    assert(archive != NULL && file != NULL);

    int result = EE_ERROR;
    struct exar_header_s header = EXAR_HEADER_EMPTY;
    FILE *f = NULL, *ftmp = NULL;
    char tmp_file[128];
    char dir_name[EXAR_NAME_MAX-1] = {0};
    unsigned char rbuf;
    size_t dir_length = 0;
    int status = EE_ERROR;

    if ((f = open_archive(archive, "r")) == NULL)
        goto finish;

    snprintf(tmp_file, sizeof(tmp_file), "%s.XXXXXX", archive);
    if (mktemp(tmp_file) == NULL)
    {
        fprintf(stderr, "Failed to create temporary file\n");
        goto finish;
    }

    LOG(3, "Opening %s for writing\n", tmp_file);
    if ((ftmp = fopen(tmp_file, "w")) == NULL)
        goto finish;

    while ((status = get_file_header(f, &header)) == EE_OK)
    {
        if (strcmp(header.eh_name, file) == 0)
        {
            if (header.eh_flag == FILE_FLAG)
            {
                LOG(1, "Skipping %s\n", header.eh_name);
                fseek(f, header.eh_size, SEEK_CUR);
            }
            else if (header.eh_flag == DIR_FLAG)
                dir_length = snprintf(dir_name, sizeof(dir_name), "%s/", header.eh_name);
        }
        else if (*dir_name && strncmp(dir_name, header.eh_name, dir_length) == 0)
        {
            if (header.eh_flag == FILE_FLAG)
            {
                LOG(1, "Skipping %s\n", header.eh_name);
                fseek(f, header.eh_size, SEEK_CUR);
            }
        }
        else 
        {
            LOG(1, "Packing %s\n", header.eh_name);
            write_file_header(ftmp, header.eh_name, header.eh_flag, header.eh_size);
            if (header.eh_flag == FILE_FLAG)
            {
                LOG(2, "Copying %s (%jd bytes)\n", header.eh_name, (intmax_t)header.eh_size);
                for (off_t s=0; s<header.eh_size; s++)
                {
                    if (fread(&rbuf, 1, 1, f) != 1 || fwrite(&rbuf, 1, 1, ftmp) != 1)
                    {
                        fprintf(stderr, "Error copying %s\n", header.eh_name);
                        goto finish;
                    }
                }
            }
        }
    }
finish:
    if (status == EE_EOF)
    {
        LOG(2, "Copying %s to %s\n", tmp_file, archive);
        if (rename(tmp_file, archive) == -1)
            perror(archive);
        else 
            result = EE_OK;
    }
    else if (status == EE_ERROR)
    {
        LOG(1, "An error occured, removing temporary file\n");
        unlink(tmp_file);
    }
    close_file(f, archive);
    close_file(ftmp, tmp_file);
    return result;
}
void 
exar_info(const char *archive)
{
    assert(archive != NULL);

    FILE *f = NULL;
    struct exar_header_s header = EXAR_HEADER_EMPTY;

    if ((f = open_archive(archive, "r")) == NULL)
        goto finish;
    while(next_file(f, &header) == EE_OK)
        fprintf(stdout, "%c %-14jd %s\n", header.eh_flag, (intmax_t)header.eh_size, header.eh_name);
finish:
    close_file(f, archive);
}
int
exar_contains(const char *archive, const char *name)
{
    assert(archive != NULL);

    return contains(archive, name, strcmp);
}
int
exar_search_contains(const char *archive, const char *name)
{
    assert(archive != NULL);

    return contains(archive, name, find_cmp);
}

int
exar_check_version(const char *archive)
{
    assert(archive != NULL);

    int result = EE_ERROR;
    FILE *f; 
    if ( (f = fopen(archive, "r")) != NULL && check_version(f, 0) == EE_OK)
        result = EE_OK; 
    close_file(f, archive);
    return result;
}
void 
exar_verbose(unsigned char v)
{
    s_verbose = v & EXAR_VERBOSE_MASK;
}
