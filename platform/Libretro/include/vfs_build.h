#pragma once

#include "vfs_libretro.h"

using buildvfs_FILE = LibretroFile *;
using buildvfs_fd   = int;

#define buildvfs_fd_invalid          (-1)
#define buildvfs_EOF                 EOF
#define buildvfs_fopen_read(p)       libretro_vfs_fopen(p, "rb")
#define buildvfs_fopen_write(p)      libretro_vfs_fopen(p, "wb")
#define buildvfs_fopen_write_text(p) libretro_vfs_fopen(p, "w")
#define buildvfs_fopen_append(p)     libretro_vfs_fopen(p, "ab+")
#define buildvfs_fread               libretro_vfs_fread
#define buildvfs_fwrite              libretro_vfs_fwrite
#define buildvfs_fclose              libretro_vfs_fclose
#define buildvfs_fflush              libretro_vfs_fflush
#define buildvfs_feof                libretro_vfs_feof
#define buildvfs_fgetc               libretro_vfs_fgetc
#define buildvfs_fputc               libretro_vfs_fputc
#define buildvfs_fgets               libretro_vfs_fgets
#define buildvfs_ftell               libretro_vfs_ftell
#define buildvfs_flength             libretro_vfs_flength
#define buildvfs_fseek_abs(f, o)     libretro_vfs_fseek(f, o, SEEK_SET)
#define buildvfs_fseek_rel(f, o)     libretro_vfs_fseek(f, o, SEEK_CUR)
#define buildvfs_fseek_end(f)        libretro_vfs_fseek(f, 0, SEEK_END)
#define buildvfs_rewind(f)           libretro_vfs_fseek(f, 0, SEEK_SET)
#define buildvfs_open_read(p)        libretro_vfs_open(p, O_RDONLY | O_BINARY)
#define buildvfs_open_write(p)       libretro_vfs_open(p, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY)
#define buildvfs_open_append(p)      libretro_vfs_open(p, O_WRONLY | O_CREAT | O_APPEND | O_BINARY)
#define buildvfs_read                libretro_vfs_read
#define buildvfs_write               libretro_vfs_write
#define buildvfs_close               libretro_vfs_close
#define buildvfs_length              libretro_vfs_length
#define buildvfs_tell(f)             libretro_vfs_seek(f, 0, SEEK_CUR)
#define buildvfs_lseek_abs(f, o)     libretro_vfs_seek(f, o, SEEK_SET)
#define buildvfs_lseek_rel(f, o)     libretro_vfs_seek(f, o, SEEK_CUR)
#define buildvfs_unlink              libretro_vfs_remove
#define buildvfs_mkdir               libretro_vfs_mkdir

#define buildvfs_chdir  chdir
#define buildvfs_getcwd getcwd

static inline int buildvfs_exists(const char *path)
{
    struct stat st;
    return libretro_vfs_stat(path, &st) == 0;
}

static inline int buildvfs_isdir(const char *path)
{
    struct stat st;
    return libretro_vfs_stat(path, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}
