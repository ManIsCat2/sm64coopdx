#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "save_path.h"
#include "platform.h"

bool save_path_should_migrate(bool customSaveExists, bool defaultSaveExists) {
    return !customSaveExists && defaultSaveExists;
}

bool save_path_join(char *dst, size_t dstsize, const char *dir, const char *filename) {
    if (!dst || !dir || !filename)
        return false;
    return snprintf(dst, dstsize, "%s/%s", dir, filename) < (int) dstsize;
}

static bool save_path_is_drive_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

size_t save_path_root_length(const char *path, bool windows) {
    if (path == NULL || path[0] == '\0')
        return 0;
    if (!windows)
        return path[0] == '/' ? 1 : 0;

    if (save_path_is_drive_letter(path[0]) && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) {
        return 3;
    }

    if ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/')) {
        const char *serverEnd = strpbrk(path + 2, "/\\");
        if (serverEnd == NULL || serverEnd == path + 2)
            return 0;
        const char *share = serverEnd + 1;
        const char *shareEnd = strpbrk(share, "/\\");
        if (*share == '\0')
            return 0;
        return shareEnd != NULL ? (size_t) (shareEnd - path + 1) : strlen(path);
    }

    return 0;
}

enum save_path_validation save_path_validate(const char *path, size_t maxlen) {
    if (path == NULL) {
        return SAVE_PATH_ERR_EMPTY;
    }

    size_t len = strlen(path);
    if (len == 0) {
        return SAVE_PATH_ERR_EMPTY;
    }
    if (len >= maxlen) {
        return SAVE_PATH_ERR_TOO_LONG;
    }

    bool windows = PATH_SEPARATOR[0] == '\\';
    if (save_path_root_length(path, windows) == 0) {
        return SAVE_PATH_ERR_RELATIVE;
    }

    // reject control characters (newline, tab, ...): they cannot survive the
    // config file format or be typed meaningfully into a directory path
    for (const char *c = path; *c != '\0'; c++) {
        unsigned char ch = (unsigned char) *c;
        if (ch < 32 || ch == 127) {
            return SAVE_PATH_ERR_BAD_CHAR;
        }
    }

    return SAVE_PATH_VALID;
}

static const char sSaveLocationConfigKey[] = "save_location";

bool save_path_parse_config_value(char *dst, size_t dstsize, const char *line) {
    if (dst == NULL || dstsize == 0 || line == NULL) {
        return false;
    }

    while (isspace((unsigned char) *line)) {
        line++;
    }

    size_t keyLen = sizeof(sSaveLocationConfigKey) - 1;
    if (strncmp(line, sSaveLocationConfigKey, keyLen) != 0) {
        return false;
    }

    // the key must be followed by whitespace or end-of-line, so that other
    // options merely sharing the prefix (e.g. "save_location2") do not match
    const char *rest = line + keyLen;
    if (*rest != '\0' && !isspace((unsigned char) *rest)) {
        return false;
    }

    while (isspace((unsigned char) *rest)) {
        rest++;
    }

    // the raw remainder keeps any internal spacing; only outer whitespace is
    // trimmed so a quoted value round-trips exactly
    const char *end = rest + strlen(rest);
    while (end > rest && isspace((unsigned char) end[-1])) {
        end--;
    }

    if (end - rest >= 2 && rest[0] == '"' && end[-1] == '"') {
        rest++;
        end--;
    }

    size_t len = (size_t) (end - rest);
    if (len >= dstsize) {
        len = dstsize - 1;
    }
    memcpy(dst, rest, len);
    dst[len] = '\0';
    return true;
}

void save_path_format_config_value(char *dst, size_t dstsize, const char *value) {
    if (dst == NULL || dstsize == 0) {
        return;
    }
    if (value == NULL) {
        value = "";
    }

    bool needsQuotes = false;
    for (const char *c = value; *c != '\0'; c++) {
        if (isspace((unsigned char) *c)) {
            needsQuotes = true;
            break;
        }
    }

    if (needsQuotes) {
        snprintf(dst, dstsize, "\"%s\"", value);
    } else {
        snprintf(dst, dstsize, "%s", value);
    }
}
