#ifndef SM64_SAVE_PATH_H_
#define SM64_SAVE_PATH_H_

#include <stdbool.h>
#include <stddef.h>

// Pure decision logic for the custom save-location feature, kept free of
// filesystem/config dependencies so it can be compiled and validated standalone.

// Migrate (copy) the default save into the custom location only when the
// custom-location save is missing and a default save exists. Never deletes
// or overwrites either copy: this keeps the operation safe to retry and keeps
// the old save available as a fallback if the custom location later fails.
bool save_path_should_migrate(bool customSaveExists, bool defaultSaveExists);

// Joins "<dir>/<filename>" into dst. Returns false (dst left untouched) if the
// result would not fit in dstsize.
bool save_path_join(char *dst, size_t dstsize, const char *dir, const char *filename);

// Returns the length of an absolute path root (POSIX slash, Windows drive, or
// complete UNC share), or zero when path is not absolute for that platform.
// The explicit platform argument keeps Windows root handling unit-testable on
// non-Windows build hosts.
size_t save_path_root_length(const char *path, bool windows);

// Result of validating a user-supplied save directory.
enum save_path_validation {
    SAVE_PATH_VALID = 0,
    SAVE_PATH_ERR_EMPTY,    // no directory given
    SAVE_PATH_ERR_RELATIVE, // not an absolute path
    SAVE_PATH_ERR_BAD_CHAR, // control characters (newline, tab, ...) that
                            // cannot survive the config file or the path APIs
    SAVE_PATH_ERR_TOO_LONG, // does not fit in a buffer of maxlen bytes
};

// Checks that path is a non-empty absolute directory path (POSIX root, UNC
// share, or Windows drive) made of reasonable characters, short enough to fit
// (with its terminator) in a buffer of maxlen bytes.
enum save_path_validation save_path_validate(const char *path, size_t maxlen);

// If line is a "save_location <value>" config line, extracts value into dst
// (unquoted, whitespace-trimmed, truncated to dstsize-1) and returns true.
// Handles both the legacy unquoted form (the whole remainder of the line) and
// the quoted form written by save_path_format_config_value, so values keep
// their spaces. Returns false for any other line, including keys that merely
// share the prefix (e.g. "save_location2").
bool save_path_parse_config_value(char *dst, size_t dstsize, const char *line);

// Formats a config value for writing: quoted when it contains whitespace,
// plain otherwise, so the value round-trips through
// save_path_parse_config_value unchanged.
void save_path_format_config_value(char *dst, size_t dstsize, const char *value);

#endif // SM64_SAVE_PATH_H_
