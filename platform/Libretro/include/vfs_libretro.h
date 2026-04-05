#pragma once

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

void libretro_vfs_set_directory(const char *path);

int     libretro_vfs_open(const char *path, int flags, int mode = 0666);
int     libretro_vfs_close(int fd);
int64_t libretro_vfs_read(int fd, void *data, size_t size);
int64_t libretro_vfs_write(int fd, const void *data, size_t size);
int64_t libretro_vfs_seek(int fd, int64_t offset, int whence);
int64_t libretro_vfs_length(int fd);
int     libretro_vfs_stat(const char *path, struct stat *st);
int     libretro_vfs_fstat(int fd, struct stat *st);

struct libretro_stat : public stat
{
};

inline int libretro_stat(const char *path, struct libretro_stat *st)
{
    return libretro_vfs_stat(path, st);
}

int libretro_vfs_mkdir(const char *path, int mode);
int libretro_vfs_remove(const char *path);

struct LibretroFile;

LibretroFile *libretro_vfs_fopen(const char *path, const char *mode);
int           libretro_vfs_fclose(LibretroFile *file);
size_t        libretro_vfs_fread(void *data, size_t size, size_t count, LibretroFile *file);
size_t        libretro_vfs_fwrite(const void *data, size_t size, size_t count, LibretroFile *file);
int           libretro_vfs_fseek(LibretroFile *file, int64_t offset, int whence);
int64_t       libretro_vfs_ftell(LibretroFile *file);
int64_t       libretro_vfs_flength(LibretroFile *file);
int           libretro_vfs_fflush(LibretroFile *file);
int           libretro_vfs_feof(LibretroFile *file);
int           libretro_vfs_fgetc(LibretroFile *file);
int           libretro_vfs_fputc(int ch, LibretroFile *file);
char         *libretro_vfs_fgets(char *data, int size, LibretroFile *file);
int           libretro_vfs_fprintf(LibretroFile *file, const char *format, ...);
