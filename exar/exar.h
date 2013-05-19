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

/* 
 * Pack/Unpack files/directories to dwb-extension archive. Only directories and
 * regular files are supported. Unlike tar exar doesn't preserve symbolic links,
 * instead a copy is packed into the archive and an extension archive doesn't
 * preserve any ownership or permissions, so an extension archive cannot contain
 * any executables.
 *
 * File format:
 *
 * [version header][file header][file][version header][file header][file][version ...
 *
 * file header    : 128 bytes
 *                  - version              : 8 bytes
 *                  - filename             : 100 bytes, (char*)
 *                  - directory flag(d|f)  : 1 byte     (char)
 *                  - file size            : 12 bytes   (char*, octal representation)
 *                  - checksum:            : 7 bytes    (char*, octal representation)
 * */

#ifndef __EXAR_H__
#define __EXAR_H__

enum {
    EXAR_VERBOSE_L1 = 1<<0,
    EXAR_VERBOSE_L2 = 1<<1,
    EXAR_VERBOSE_L3 = 1<<2,
};
#define EXAR_VERBOSE_MASK (0x7)

/* 
 * Packs a file or directory
 * @path: Path to the file or directory to pack
 *
 * @returns 0 on success and -1 on error
 * */
int 
exar_pack(const char *path);

/*
 * Unpacks an archive
 * @path: Path to the extension archive
 * @dest: Destination directory or NULL for current directory
 *
 * @returns 0 on success and -1 on error
 * */
int 
exar_unpack(const char *path, const char *dest);

/* 
 * Extracts a file from an extension archive
 * @archive The archive
 * @file    The path of the file in the archive
 * @size    Return location for the size, if an error occurs size will be set to -1
 *
 * @returns A newly allocated char buffer with the file content or NULL if an error
 *          occured or the file was not found in the archive
 * */
unsigned char * 
exar_extract(const char *archive, const char *file, size_t *size);

/* 
 * Searches for a file and extracts the content from the archive. 
 * 
 * @archive The archive
 * @search  The search term. The term is is compared with the end of each
 *          filename in the archive
 * @size    Return location for the size, if an error occurs size will be set to -1
 *
 * @returns A newly allocated char buffer with the file content or NULL if an error
 *          occured or the file was not found in the archive
 * */
unsigned char * 
exar_search_extract(const char *archive, const char *search, size_t *size);

/*
 * Deletes a file from the archive, if it is a directory it is removed
 * recursively.
 *
 * @archive  The archive
 * @file     The file to delete
 *
 * @returns 0 on success and -1 on error
 */
int 
exar_delete(const char *archive, const char *file);

/*
 * Checks if the file is an archive file with compatible version number
 *
 * @archive  The archive
 * @verbose  Whether to print error messages to stderr
 *
 * @returns 0 on success and -1 on error
 */
int 
exar_check_version(const char *archive);

/*
 * Print info about the archive to stdout.
 *
 * @archive  The archive
 * */
void 
exar_info(const char *archive);

/*
 * Set verbosity flags, exar will be most verbose if all flags are set, log
 * messages are printed to stderr.
 * @v_flags 
 */
void 
exar_verbose(unsigned char v_flags);
#endif
