/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "paths.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"

namespace kisimavr
{

std::string resolvePath(const std::string &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || fs::exists(path, ec))
    {
        return path;
    }

    const fs::path in(path);
    fs::path out = in.root_path();
    for (const fs::path &part : in.relative_path())
    {
        fs::path candidate = out / part;
        if (!fs::exists(candidate, ec))
        {
            const std::string wanted = toLower(part.string());
            const fs::path dir = out.empty() ? fs::path(".") : out;
            bool found = false;
            for (const fs::directory_entry &entry : fs::directory_iterator(dir, ec))
            {
                if (toLower(entry.path().filename().string()) == wanted)
                {
                    candidate = out / entry.path().filename();
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return path;    // Really missing: report the original name.
            }
        }
        out = candidate;
    }
    return out.string();
}

std::string resolveOutputPath(const std::string &path)
{
    namespace fs = std::filesystem;
    const fs::path in(path);
    if (!in.has_parent_path())
    {
        return path;
    }
    return (fs::path(resolvePath(in.parent_path().string())) / in.filename()).string();
}

bool looksLikeVcd(const std::string &head)
{
    return head.rfind("$timescale", 0) == 0;
}

bool looksLikeLcdLog(const std::string &head)
{
    return head.rfind("# kisimavr LCD", 0) == 0;
}

bool looksLikeUartLog(const std::string &head)
{
    size_t lineLength = 0;
    for (const char c : head)
    {
        if (c == '\n')
        {
            lineLength = 0;
            continue;
        }
        if (c < 0x20 || c > 0x7E || ++lineLength > 200u)
        {
            return false;
        }
    }
    return true;
}

bool hasExtension(const std::string &path, std::initializer_list<const char *> extensions)
{
    const std::string ext = toLower(std::filesystem::path(path).extension().string());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&ext](const char *e) { return ext == e; });
}

std::FILE *openOutputFile(const std::string &path, const char *what,
                          std::initializer_list<const char *> extensions, ContentCheck check)
{
    auto refuse = [&](const char *reason) {
        std::fprintf(stderr, "%s not writing %s '%s': %s\n", kTag, what, path.c_str(), reason);
        return static_cast<std::FILE *>(nullptr);
    };

    if (!hasExtension(path, extensions))
    {
        return refuse("unexpected file extension");
    }

    // O_NOFOLLOW: no symbolic link; O_NONBLOCK: never hang on a FIFO.
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                          0644);
    if (fd < 0)
    {
        return refuse(errno == ELOOP ? "is a symbolic link" : std::strerror(errno));
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
    {
        ::close(fd);
        return refuse("not a regular file");
    }

    if (st.st_size > 0)
    {
        std::string head(4096, '\0');
        const ssize_t n = ::read(fd, &head[0], head.size());
        head.resize(n > 0 ? static_cast<size_t>(n) : 0u);
        if (!check(head))
        {
            ::close(fd);
            return refuse("existing file was not written by mcu_cosim (delete it to reuse the name)");
        }
    }

    if (::ftruncate(fd, 0) != 0 || ::lseek(fd, 0, SEEK_SET) != 0 ||
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) & ~O_NONBLOCK) != 0)
    {
        ::close(fd);
        return refuse(std::strerror(errno));
    }

    std::FILE *file = ::fdopen(fd, "w");
    if (file == nullptr)
    {
        ::close(fd);
        return refuse(std::strerror(errno));
    }
    return file;
}

} // namespace kisimavr
