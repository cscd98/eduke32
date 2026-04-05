#include "vfs_libretro.h"

#include "compat.h"
#include "core.h"

#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr int firstVfsFd = 0x40000000;

struct VfsFile
{
    retro_vfs_interface const *api;
    retro_vfs_file_handle     *handle;
    std::string                path;
    bool                       append;
};

std::map<int, VfsFile> files;
std::mutex             filesMutex;
std::string            saveDirectory;

std::string resolvePath(const char *path)
{
    if (saveDirectory.empty() || !*path || path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':') || strstr(path, "://"))
        return path;

    while (path[0] == '.' && path[1] == '/')
        path += 2;

    return saveDirectory + "/" + path;
}

int fail(int error)
{
    errno = error;
    return -1;
}

int statWith(retro_vfs_interface const *api, const char *path, struct stat *st)
{
    int64_t size  = 0;
    int     flags = api->stat_64(path, &size);
    if (!(flags & RETRO_VFS_STAT_IS_VALID))
        return fail(ENOENT);

    *st           = {};
    st->st_mode   = (flags & RETRO_VFS_STAT_IS_DIRECTORY) ? S_IFDIR : S_IFREG;
    st->st_size   = size;
    int64_t mtime = 0;

    if (api->get_mtime && api->get_mtime(path, &mtime) == 0)
        st->st_mtime = mtime;

    return 0;
}
}  // namespace

void libretro_vfs_set_directory(const char *path)
{
    saveDirectory = path ? path : "";
}

int libretro_vfs_open(const char *path, int flags, int mode)
{
    auto const *api = vfs_iface;

    if (!api)
        return open(path, flags, mode);

    auto resolved = resolvePath(path);
    path          = resolved.c_str();

    unsigned access = RETRO_VFS_FILE_ACCESS_READ;
    if ((flags & O_ACCMODE) == O_WRONLY)
        access = RETRO_VFS_FILE_ACCESS_WRITE;
    else if ((flags & O_ACCMODE) == O_RDWR)
        access = RETRO_VFS_FILE_ACCESS_READ_WRITE;

    struct stat st;
    bool const  exists = statWith(api, path, &st) == 0;
    if (!exists && !(flags & O_CREAT))
        return fail(ENOENT);
    if (exists && (flags & O_CREAT) && (flags & O_EXCL))
        return fail(EEXIST);
    if (exists && !(flags & O_TRUNC) && access != RETRO_VFS_FILE_ACCESS_READ)
        access |= RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;

    auto *handle = api->open(path, access, RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!handle)
        return fail(EIO);

    std::lock_guard<std::mutex> lock(filesMutex);
    int                         fd = firstVfsFd;
    while (files.count(fd))
    {
        if (fd == INT_MAX)
        {
            api->close(handle);
            return fail(EMFILE);
        }
        ++fd;
    }

    files.emplace(fd, VfsFile { api, handle, path, (flags & O_APPEND) != 0 });

    return fd;
}

int libretro_vfs_close(int fd)
{
    if (fd < firstVfsFd)
        return close(fd);

    std::lock_guard<std::mutex> lock(filesMutex);
    auto                        it = files.find(fd);
    if (it == files.end())
        return fail(EBADF);

    int result = it->second.api->close(it->second.handle);
    files.erase(it);

    return result;
}

int64_t libretro_vfs_read(int fd, void *data, size_t size)
{
    if (fd < firstVfsFd)
        return read(fd, data, size);

    std::lock_guard<std::mutex> lock(filesMutex);
    auto                        it = files.find(fd);
    if (it == files.end())
        return fail(EBADF);

    return it->second.api->read(it->second.handle, data, size);
}

int64_t libretro_vfs_write(int fd, const void *data, size_t size)
{
    if (fd < firstVfsFd)
        return write(fd, data, size);

    std::lock_guard<std::mutex> lock(filesMutex);
    auto                        it = files.find(fd);
    if (it == files.end())
        return fail(EBADF);

    auto &f = it->second;
    if (f.append && f.api->seek(f.handle, 0, RETRO_VFS_SEEK_POSITION_END) < 0)
        return -1;

    return f.api->write(f.handle, data, size);
}

int64_t libretro_vfs_seek(int fd, int64_t offset, int whence)
{
    if (fd < firstVfsFd)
        return lseek(fd, offset, whence);

    int origin;
    switch (whence)
    {
        case SEEK_SET:
            origin = RETRO_VFS_SEEK_POSITION_START;
            break;
        case SEEK_CUR:
            origin = RETRO_VFS_SEEK_POSITION_CURRENT;
            break;
        case SEEK_END:
            origin = RETRO_VFS_SEEK_POSITION_END;
            break;
        default:
            return fail(EINVAL);
    }

    std::lock_guard<std::mutex> lock(filesMutex);
    auto                        it = files.find(fd);

    if (it == files.end())
        return fail(EBADF);

    return it->second.api->seek(it->second.handle, offset, origin);
}

int libretro_vfs_stat(const char *path, struct stat *st)
{
    return vfs_iface ? statWith(vfs_iface, resolvePath(path).c_str(), st) : stat(path, st);
}

int libretro_vfs_fstat(int fd, struct stat *st)
{
    if (fd < firstVfsFd)
        return fstat(fd, st);

    std::lock_guard<std::mutex> lock(filesMutex);
    auto                        it = files.find(fd);
    if (it == files.end())
        return fail(EBADF);

    auto &f = it->second;
    // Query the handle's size, including writes not reflected in path metadata.
    *st         = {};
    st->st_mode = S_IFREG;
    auto size   = f.api->size(f.handle);
    if (size < 0)
        return fail(EIO);

    st->st_size   = size;
    int64_t mtime = 0;

    if (f.api->get_mtime && f.api->get_mtime(f.path.c_str(), &mtime) == 0)
        st->st_mtime = mtime;

    return 0;
}

int64_t libretro_vfs_length(int fd)
{
    struct stat st;
    return libretro_vfs_fstat(fd, &st) == 0 ? st.st_size : -1;
}

int libretro_vfs_mkdir(const char *path, int mode)
{
    if (vfs_iface)
    {
        int result = vfs_iface->mkdir(resolvePath(path).c_str());
        return result == -2 ? fail(EEXIST) : result;
    }
    return Bmkdir(path, mode);
}

int libretro_vfs_remove(const char *path)
{
    return vfs_iface ? vfs_iface->remove(resolvePath(path).c_str()) : unlink(path);
}

struct LibretroFile
{
    int  fd;
    bool eof;
};

LibretroFile *libretro_vfs_fopen(const char *path, const char *mode)
{
    if (!mode || !*mode)
    {
        fail(EINVAL);
        return nullptr;
    }

    bool const plus  = strchr(mode, '+') != nullptr;
    int        flags = strchr(mode, 'b') ? O_BINARY : 0;

    switch (*mode)
    {
        case 'r':
            flags |= plus ? O_RDWR : O_RDONLY;
            break;
        case 'w':
            flags |= (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
            break;
        case 'a':
            flags |= (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
            break;
        default:
            fail(EINVAL);
            return nullptr;
    }

    int fd = libretro_vfs_open(path, flags);
    if (fd < 0)
        return nullptr;

    return new LibretroFile { fd, false };
}

int libretro_vfs_fclose(LibretroFile *file)
{
    if (!file)
        return fail(EBADF);

    int result = libretro_vfs_close(file->fd);
    delete file;

    return result;
}

size_t libretro_vfs_fread(void *data, size_t size, size_t count, LibretroFile *file)
{
    if (!size || !count)
        return 0;

    if (!file || count > SIZE_MAX / size)
    {
        fail(EINVAL);
        return 0;
    }

    size_t bytes = 0, total = size * count;
    while (bytes < total)
    {
        auto n = libretro_vfs_read(file->fd, static_cast<char *>(data) + bytes, total - bytes);
        if (n <= 0)
        {
            if (n == 0)
                file->eof = true;
            break;
        }

        bytes += n;
    }

    return bytes / size;
}

size_t libretro_vfs_fwrite(const void *data, size_t size, size_t count, LibretroFile *file)
{
    if (!size || !count)
        return 0;

    if (!file || count > SIZE_MAX / size)
    {
        fail(EINVAL);
        return 0;
    }

    size_t bytes = 0, total = size * count;
    while (bytes < total)
    {
        auto n = libretro_vfs_write(file->fd, static_cast<const char *>(data) + bytes, total - bytes);
        if (n <= 0)
            break;
        bytes += n;
    }

    return bytes / size;
}

int libretro_vfs_fseek(LibretroFile *file, int64_t offset, int whence)
{
    if (!file || libretro_vfs_seek(file->fd, offset, whence) < 0)
        return -1;

    file->eof = false;

    return 0;
}

int64_t libretro_vfs_ftell(LibretroFile *file)
{
    return libretro_vfs_seek(file->fd, 0, SEEK_CUR);
}

int64_t libretro_vfs_flength(LibretroFile *file)
{
    return libretro_vfs_length(file->fd);
}

int libretro_vfs_feof(LibretroFile *file)
{
    return file->eof;
}

int libretro_vfs_fflush(LibretroFile *file)
{
    if (!file)
        return fail(EBADF);

    if (file->fd < firstVfsFd)
        return 0;

    std::lock_guard<std::mutex> lock(filesMutex);
    auto                        it = files.find(file->fd);

    if (it == files.end())
        return fail(EBADF);

    return it->second.api->flush(it->second.handle);
}

int libretro_vfs_fgetc(LibretroFile *file)
{
    unsigned char ch;
    return libretro_vfs_fread(&ch, 1, 1, file) == 1 ? ch : EOF;
}

int libretro_vfs_fputc(int ch, LibretroFile *file)
{
    unsigned char byte = ch;
    return libretro_vfs_fwrite(&byte, 1, 1, file) == 1 ? byte : EOF;
}

char *libretro_vfs_fgets(char *data, int size, LibretroFile *file)
{
    if (size <= 0)
        return nullptr;

    int i = 0;

    while (i < size - 1)
    {
        int ch = libretro_vfs_fgetc(file);
        if (ch == EOF)
            break;

        data[i++] = ch;
        if (ch == '\n')
            break;
    }

    data[i] = 0;
    return i || size == 1 ? data : nullptr;
}

int libretro_vfs_fprintf(LibretroFile *file, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);

    if (count < 0)
    {
        va_end(args);
        return -1;
    }

    std::vector<char> text(static_cast<size_t>(count) + 1);
    vsnprintf(text.data(), text.size(), format, args);
    va_end(args);

    return libretro_vfs_fwrite(text.data(), 1, count, file) == static_cast<size_t>(count) ? count : -1;
}

extern "C"
{
BDIR    *libretro_native_opendir(const char *path);
Bdirent *libretro_native_readdir(BDIR *dir);
int32_t  libretro_native_closedir(BDIR *dir);
}

struct VfsDir
{
    retro_vfs_interface const *api;
    retro_vfs_dir_handle      *handle;
    BDIR                      *native;
    std::string                path;
    Bdirent                    entry;
};

BDIR *Bopendir(const char *path)
{
    auto *dir = new VfsDir { vfs_iface, nullptr, nullptr, vfs_iface ? resolvePath(path) : path, {} };

    if (dir->api)
        dir->handle = dir->api->opendir(dir->path.c_str(), true);
    else
        dir->native = libretro_native_opendir(path);

    if (!dir->handle && !dir->native)
    {
        delete dir;
        return nullptr;
    }

    return dir;
}

Bdirent *Breaddir(BDIR *opaque)
{
    auto *dir = static_cast<VfsDir *>(opaque);
    if (!dir->api)
        return libretro_native_readdir(dir->native);

    auto *api = dir->api;
    if (!api->readdir(dir->handle))
        return nullptr;

    auto *name = api->dirent_get_name(dir->handle);

    if (!name)
        return nullptr;
    int64_t size = 0, mtime = 0;

    int flags = api->dirent_stat ? api->dirent_stat(dir->handle, &size, &mtime) : 0;
    if (!(flags & RETRO_VFS_STAT_IS_VALID))
    {
        struct stat st;
        std::string path = dir->path + "/" + name;
        if (statWith(api, path.c_str(), &st) == 0)
        {
            size  = st.st_size;
            mtime = st.st_mtime;
        }

        flags = api->dirent_is_dir(dir->handle) ? RETRO_VFS_STAT_IS_DIRECTORY : 0;
    }
    dir->entry        = {};
    dir->entry.name   = const_cast<char *>(name);
    dir->entry.namlen = strlen(name);
    dir->entry.mode   = (flags & RETRO_VFS_STAT_IS_DIRECTORY) ? S_IFDIR : S_IFREG;
    dir->entry.size   = size;
    dir->entry.mtime  = mtime;

    return &dir->entry;
}

int32_t Bclosedir(BDIR *opaque)
{
    auto *dir    = static_cast<VfsDir *>(opaque);
    int   result = dir->api ? dir->api->closedir(dir->handle) : libretro_native_closedir(dir->native);
    delete dir;

    return result;
}
