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
 * [version header][file header][file][file header][file][file header...
 *
 * version header : 8 bytes
 * file header    : 128 bytes
 *                  - filename             : 115 bytes, (char*)
 *                  - directory flag(d|f)  : 1 byte     (char)
 *                  - file size            : 12 bytes   (unsigned int, octal)
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
 * Set verbosity flags, exar will be most verbose if all flags are set
 * @v_flags 
 */
void 
exar_verbose(unsigned char v_flags);


/* 
 * Packs a file or directory
 * @path: Path to the file to pack
 *
 * @returns 0 on success and -1 on error
 * */
int 
exar_pack(const char *path);

/* 
 * Concatenates two archives or an archive and a file or directory
 * @file1: The archive to append
 * @file2: The archive, file or directory that will be appended
 *
 * @returns 0 on success and -1 on error
 * */
int 
exar_cat(const char *file1, const char *file2);

/*
 * Unpacks a file
 * @path: Path to the extension archive
 * @dest: Destination directory or NULL for current directory
 *
 * @returns 0 on success and -1 on error
 * */
int 
exar_unpack(const char *path, const char *dest);

#endif
