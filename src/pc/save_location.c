#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "save_location.h"
#include "save_path.h"
#include "platform.h"
#include "fs/fs.h"
#include "save_location_config.h"

#define SAVE_LOCATION_LOG_ERROR(...)                                                                   \
    do {                                                                                               \
        fprintf(stderr, __VA_ARGS__);                                                                  \
        fputc('\n', stderr);                                                                           \
    } while (0)

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h> // _S_IREAD / _S_IWRITE for _open
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h> // SYS_renameat2 (raw syscall; see publish below)
#endif
#endif

// renameat2(2) UAPI flag. Defined here because libc headers only expose it on
// newer glibc (2.28+) and bionic (API 30+); the raw syscall itself works on any
// Linux with a 3.15+ kernel.
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif

#ifdef SAVE_LOCATION_TEST
static enum save_location_test_fault sTestFault;
static void (*sTestBeforePublish)(const char *dst);
static int sTestRenameat2Errno; // 0 = real syscall; otherwise simulated failure
static enum save_location_test_publish sTestLastPublish;

void save_location_test_set_fault(enum save_location_test_fault fault) {
    sTestFault = fault;
}
void save_location_test_set_before_publish(void (*callback)(const char *dst)) {
    sTestBeforePublish = callback;
}
void save_location_test_set_renameat2_errno(int error) {
    sTestRenameat2Errno = error;
}
enum save_location_test_publish save_location_test_last_publish(void) {
    return sTestLastPublish;
}
#define SAVE_LOCATION_FAULT(fault) (sTestFault == (fault))
#else
#define SAVE_LOCATION_FAULT(fault) false
#endif

static bool save_location_is_separator(char c) {
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

// Creates every missing component without ever treating a drive prefix or a
// partial UNC path as a relative directory.
static bool save_location_mkdir_recursive(char *path) {
    if (fs_sys_dir_exists(path))
        return true;
#if defined(_WIN32)
    const bool windows = true;
#else
    const bool windows = false;
#endif
    size_t rootLen = save_path_root_length(path, windows);
    if (rootLen == 0)
        return false;

    for (char *c = path + rootLen; *c != '\0'; c++) {
        if (!save_location_is_separator(*c))
            continue;
        if (c == path + rootLen || save_location_is_separator(c[-1]))
            continue;
        char separator = *c;
        *c = '\0';
        bool ok = fs_sys_dir_exists(path) || fs_sys_mkdir(path);
        *c = separator;
        if (!ok)
            return false;
    }
    return fs_sys_dir_exists(path) || fs_sys_mkdir(path);
}

static unsigned int sTempCounter;

static FILE *save_location_create_temp(const char *dst, char *tmp, size_t tmpSize) {
#if defined(_WIN32)
    unsigned long processId = (unsigned long) _getpid();
#else
    unsigned long processId = (unsigned long) getpid();
#endif
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        unsigned int id = ++sTempCounter;
        int len = snprintf(tmp, tmpSize, "%s.tmp.%lu.%u", dst, processId, id);
        if (len < 0 || (size_t) len >= tmpSize)
            return NULL;
#if defined(_WIN32)
        int fd = _open(tmp, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd >= 0) {
            FILE *file = _fdopen(fd, "wb");
            if (file != NULL)
                return file;
            _close(fd);
            remove(tmp);
            return NULL;
        }
#else
        int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 384);
        if (fd >= 0) {
            FILE *file = fdopen(fd, "wb");
            if (file != NULL)
                return file;
            close(fd);
            remove(tmp);
            return NULL;
        }
#endif
        if (errno != EEXIST)
            return NULL;
    }
    return NULL;
}

static bool save_location_sync_file(FILE *file) {
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

// Writes a complete, durable sibling temporary file. The caller owns tmp only
// after this function succeeds; every failure closes streams and removes it.
static bool save_location_copy_to_temp(const char *src, const char *dst, char *tmp, size_t tmpSize) {
    FILE *in = fopen(src, "rb");
    if (in == NULL)
        return false;

    FILE *out = save_location_create_temp(dst, tmp, tmpSize);
    if (out == NULL) {
        fclose(in);
        return false;
    }

    char buf[65536];
    bool ok = true;
    while (ok) {
        size_t count = fread(buf, 1, sizeof(buf), in);
        if (count > 0 && fwrite(buf, 1, count, out) != count)
            ok = false;
        if (count < sizeof(buf)) {
            if (ferror(in))
                ok = false;
            break;
        }
    }
    if (ferror(out))
        ok = false;

    if (SAVE_LOCATION_FAULT(SAVE_LOCATION_TEST_FAULT_FLUSH) || fflush(out) != 0)
        ok = false;
    if (ok && (SAVE_LOCATION_FAULT(SAVE_LOCATION_TEST_FAULT_SYNC) || !save_location_sync_file(out)))
        ok = false;

    int outClose = fclose(out);
    if (SAVE_LOCATION_FAULT(SAVE_LOCATION_TEST_FAULT_CLOSE))
        outClose = EOF;
    if (outClose != 0)
        ok = false;
    if (fclose(in) != 0)
        ok = false;

    if (!ok)
        remove(tmp);
    return ok;
}

enum save_location_publish_result {
    SAVE_LOCATION_PUBLISH_OK,
    SAVE_LOCATION_PUBLISH_EXISTS,
    SAVE_LOCATION_PUBLISH_ERROR,
};

#if !defined(_WIN32)
// Atomically publishes without replacing an existing destination using
// renameat2(RENAME_NOREPLACE). Invoked through the raw syscall because bionic
// only wraps renameat2 since API 30 (minSdk is 24) and glibc since 2.28. This
// is the only no-clobber primitive on filesystems that reject hard links,
// such as Android's /storage/emulated/0 FUSE view.
static int save_location_renameat2_noreplace(const char *tmp, const char *dst) {
#ifdef SAVE_LOCATION_TEST
    if (sTestRenameat2Errno != 0) {
        errno = sTestRenameat2Errno;
        return -1;
    }
#endif
#if defined(__linux__) && defined(SYS_renameat2)
    return (int) syscall(SYS_renameat2, AT_FDCWD, tmp, AT_FDCWD, dst,
                         (unsigned int) RENAME_NOREPLACE);
#else
    // No renameat2 on this platform (macOS, other POSIX): fall through to the
    // link(2) strategy below.
    (void) tmp;
    (void) dst;
    errno = ENOSYS;
    return -1;
#endif
}

// errno values meaning "renameat2 is unavailable here", not "publish failed":
// ENOSYS (no syscall), EINVAL (kernel/filesystem rejects the flags), or
// EOPNOTSUPP/ENOTTY (filesystem or driver lacks the operation). Anything else
// is a real failure and must not be retried with a weaker primitive.
static bool save_location_renameat2_unsupported(int error) {
    return error == ENOSYS || error == EINVAL || error == EOPNOTSUPP || error == ENOTTY;
}
#endif // !_WIN32

static enum save_location_publish_result save_location_publish_no_replace(const char *tmp,
                                                                          const char *dst) {
#ifdef SAVE_LOCATION_TEST
    sTestLastPublish = SAVE_LOCATION_TEST_PUBLISH_NONE;
    if (sTestBeforePublish != NULL)
        sTestBeforePublish(dst);
#endif
    if (SAVE_LOCATION_FAULT(SAVE_LOCATION_TEST_FAULT_PUBLISH)) {
        remove(tmp);
        return SAVE_LOCATION_PUBLISH_ERROR;
    }
#if defined(_WIN32)
    if (MoveFileExA(tmp, dst, MOVEFILE_WRITE_THROUGH) != 0)
        return SAVE_LOCATION_PUBLISH_OK;
    DWORD error = GetLastError();
    remove(tmp);
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
        // Only a regular file counts as a winning racing writer; anything
        // else squatting on the name (e.g. a directory) is unusable.
        return fs_sys_file_exists(dst) ? SAVE_LOCATION_PUBLISH_EXISTS : SAVE_LOCATION_PUBLISH_ERROR;
    }
#else
    if (save_location_renameat2_noreplace(tmp, dst) == 0) {
#ifdef SAVE_LOCATION_TEST
        sTestLastPublish = SAVE_LOCATION_TEST_PUBLISH_RENAMEAT2;
#endif
        return SAVE_LOCATION_PUBLISH_OK;
    }
    int error = errno;
    if (error == EEXIST) {
        // Only a regular file counts as a winning racing writer; anything
        // else squatting on the name (e.g. a directory) is unusable.
        remove(tmp);
        return fs_sys_file_exists(dst) ? SAVE_LOCATION_PUBLISH_EXISTS : SAVE_LOCATION_PUBLISH_ERROR;
    }
    if (!save_location_renameat2_unsupported(error)) {
        // Real failure (permissions, I/O, ...): fail safely, never clobber.
        remove(tmp);
        return SAVE_LOCATION_PUBLISH_ERROR;
    }
    // renameat2 unavailable here: link(2) is still an atomic no-clobber
    // publish wherever hard links are allowed (never replaces an existing
    // destination). Where they are not (FUSE), the publish fails safely.
    if (link(tmp, dst) == 0) {
        remove(tmp);
#ifdef SAVE_LOCATION_TEST
        sTestLastPublish = SAVE_LOCATION_TEST_PUBLISH_LINK;
#endif
        return SAVE_LOCATION_PUBLISH_OK;
    }
    error = errno;
    remove(tmp);
    if (error == EEXIST)
        return fs_sys_file_exists(dst) ? SAVE_LOCATION_PUBLISH_EXISTS : SAVE_LOCATION_PUBLISH_ERROR;
#endif
    return SAVE_LOCATION_PUBLISH_ERROR;
}

// This operation is deliberately separate from no-replace publication and is
// used only by reset after timestamp/content checks authorize replacement.
static bool save_location_publish_replace(const char *tmp, const char *dst) {
    if (SAVE_LOCATION_FAULT(SAVE_LOCATION_TEST_FAULT_PUBLISH)) {
        remove(tmp);
        return false;
    }
#if defined(_WIN32)
    bool ok = MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    bool ok = rename(tmp, dst) == 0;
#endif
    if (!ok)
        remove(tmp);
    return ok;
}

static bool save_location_copy_file_atomic(const char *src, const char *dst, bool replaceAuthorized) {
    char tmp[SYS_MAX_PATH];
    if (!save_location_copy_to_temp(src, dst, tmp, sizeof(tmp)))
        return false;
    if (replaceAuthorized)
        return save_location_publish_replace(tmp, dst);

    enum save_location_publish_result result = save_location_publish_no_replace(tmp, dst);
    return result == SAVE_LOCATION_PUBLISH_OK || result == SAVE_LOCATION_PUBLISH_EXISTS;
}

static bool save_location_files_equal(const char *first, const char *second) {
    FILE *a = fopen(first, "rb");
    if (a == NULL)
        return false;
    FILE *b = fopen(second, "rb");
    if (b == NULL) {
        fclose(a);
        return false;
    }

    bool equal = true;
    unsigned char aBuf[65536], bBuf[65536];
    while (equal) {
        size_t aCount = fread(aBuf, 1, sizeof(aBuf), a);
        size_t bCount = fread(bBuf, 1, sizeof(bBuf), b);
        if (aCount != bCount || memcmp(aBuf, bBuf, aCount) != 0)
            equal = false;
        if (aCount < sizeof(aBuf) || bCount < sizeof(bBuf)) {
            if (ferror(a) || ferror(b))
                equal = false;
            break;
        }
    }
    if (fclose(a) != 0)
        equal = false;
    if (fclose(b) != 0)
        equal = false;
    return equal;
}

// Computes the save path the game would currently use without side effects.
static bool save_location_current_path(char *dst, size_t dstSize) {
    if (configSaveLocation[0]) {
        return save_path_join(dst, dstSize, configSaveLocation, SAVE_FILENAME);
    }

    const char *defaultPath = fs_get_write_path(SAVE_FILENAME);
    if (defaultPath == NULL || strlen(defaultPath) >= dstSize)
        return false;
    snprintf(dst, dstSize, "%s", defaultPath);
    return true;
}

static const char *save_location_resolve_custom(void) {
    static char path[SYS_MAX_PATH];
    if (!configSaveLocation[0])
        return NULL;

    char customDir[SYS_MAX_PATH];
    snprintf(customDir, sizeof(customDir), "%s", configSaveLocation);
    if (!save_path_join(path, sizeof(path), customDir, SAVE_FILENAME))
        return NULL;

    if (!save_location_mkdir_recursive(customDir)) {
        SAVE_LOCATION_LOG_ERROR(
            "save_location: could not create custom save directory '%s', falling back to default",
            configSaveLocation);
        return NULL;
    }

    const char *defaultPath = fs_get_write_path(SAVE_FILENAME);
    bool customExists = fs_sys_file_exists(path);
    bool defaultExists = defaultPath != NULL && fs_sys_file_exists(defaultPath);
    if (save_path_should_migrate(customExists, defaultExists)
        && !save_location_copy_file_atomic(defaultPath, path, false)) {
        SAVE_LOCATION_LOG_ERROR("save_location: failed to migrate default save to '%s'", path);
        return NULL;
    }
    return path;
}

const char *save_location_resolve(void) {
    const char *custom = save_location_resolve_custom();
    return custom != NULL ? custom : fs_get_write_path(SAVE_FILENAME);
}

size_t save_location_read_eeprom(void *dst, size_t size) {
    const char *path = save_location_resolve();
    if (path == NULL)
        return 0;

    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    size_t count = fread(dst, 1, size, file);
    fclose(file);
    return count;
}

enum save_location_result save_location_set(const char *dir) {
    switch (save_path_validate(dir, MAX_SAVE_LOCATION_STRING)) {
        case SAVE_PATH_VALID:
            break;
        case SAVE_PATH_ERR_EMPTY:
            return SAVE_LOCATION_ERR_EMPTY;
        case SAVE_PATH_ERR_RELATIVE:
            return SAVE_LOCATION_ERR_RELATIVE;
        case SAVE_PATH_ERR_BAD_CHAR:
            return SAVE_LOCATION_ERR_BAD_CHAR;
        case SAVE_PATH_ERR_TOO_LONG:
            return SAVE_LOCATION_ERR_TOO_LONG;
    }

    char currentPath[SYS_MAX_PATH];
    if (!save_location_current_path(currentPath, sizeof(currentPath))) {
        return SAVE_LOCATION_ERR_TOO_LONG;
    }

    char targetDir[SYS_MAX_PATH];
    if (strlen(dir) >= sizeof(targetDir))
        return SAVE_LOCATION_ERR_TOO_LONG;
    snprintf(targetDir, sizeof(targetDir), "%s", dir);
    if (!save_location_mkdir_recursive(targetDir)) {
        SAVE_LOCATION_LOG_ERROR("save_location: could not create save directory '%s'", dir);
        return SAVE_LOCATION_ERR_MKDIR;
    }

    char targetPath[SYS_MAX_PATH];
    if (!save_path_join(targetPath, sizeof(targetPath), targetDir, SAVE_FILENAME)) {
        return SAVE_LOCATION_ERR_TOO_LONG;
    }

    if (!fs_sys_file_exists(targetPath)) {
        if (fs_sys_dir_exists(targetPath))
            return SAVE_LOCATION_ERR_COPY;
        const char *src = currentPath;
        if (!fs_sys_file_exists(src)) {
            const char *defaultPath = fs_get_write_path(SAVE_FILENAME);
            if (defaultPath != NULL && fs_sys_file_exists(defaultPath))
                src = defaultPath;
        }
        if (fs_sys_file_exists(src) && !save_location_copy_file_atomic(src, targetPath, false)) {
            SAVE_LOCATION_LOG_ERROR("save_location: could not copy save '%s' to '%s'", src, targetPath);
            return SAVE_LOCATION_ERR_COPY;
        }
    }

    char oldLocation[MAX_SAVE_LOCATION_STRING];
    snprintf(oldLocation, sizeof(oldLocation), "%s", configSaveLocation);
    snprintf(configSaveLocation, MAX_SAVE_LOCATION_STRING, "%s", dir);
    if (!configfile_save_checked(configfile_name())) {
        snprintf(configSaveLocation, MAX_SAVE_LOCATION_STRING, "%s", oldLocation);
        return SAVE_LOCATION_ERR_CONFIG;
    }
    return SAVE_LOCATION_OK;
}

enum save_location_result save_location_reset_to_default(void) {
    if (!configSaveLocation[0])
        return SAVE_LOCATION_OK;

    char oldLocation[MAX_SAVE_LOCATION_STRING];
    snprintf(oldLocation, sizeof(oldLocation), "%s", configSaveLocation);
    char customPath[SYS_MAX_PATH];
    if (!save_path_join(customPath, sizeof(customPath), oldLocation, SAVE_FILENAME)) {
        return SAVE_LOCATION_ERR_COPY;
    }

    const char *defaultPath = fs_get_write_path(SAVE_FILENAME);
    if (defaultPath != NULL && fs_sys_file_exists(customPath)) {
        bool defaultExists = fs_sys_file_exists(defaultPath);
        bool copyBack = !defaultExists;
        if (defaultExists) {
            uint64_t defaultTime = fs_sys_get_modified_time(defaultPath);
            uint64_t customTime = fs_sys_get_modified_time(customPath);
            if (defaultTime < customTime) {
                copyBack = true;
            } else if (defaultTime == customTime
                       && !save_location_files_equal(defaultPath, customPath)) {
                SAVE_LOCATION_LOG_ERROR(
                    "save_location: both save locations hold different data with equal timestamps; "
                    "resetting would need a manual choice");
                return SAVE_LOCATION_ERR_CONFLICT;
            }
        }
        if (copyBack && !save_location_copy_file_atomic(customPath, defaultPath, true)) {
            SAVE_LOCATION_LOG_ERROR(
                "save_location: failed to copy custom save back to default during reset");
            return SAVE_LOCATION_ERR_COPY;
        }
    }

    configSaveLocation[0] = '\0';
    if (!configfile_save_checked(configfile_name())) {
        snprintf(configSaveLocation, MAX_SAVE_LOCATION_STRING, "%s", oldLocation);
        return SAVE_LOCATION_ERR_CONFIG;
    }
    return SAVE_LOCATION_OK;
}
