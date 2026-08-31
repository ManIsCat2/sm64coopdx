// Standalone test harness for the custom save-location feature.
//
// Compiles the production modules (src/pc/save_path.c, src/pc/save_location.c)
// against minimal stubs for the config/logging dependencies so the switching,
// reset, validation, and atomic-copy logic can be tested deterministically
// without the full game build.
//
// Build & run: ./tests/run.sh
#ifndef SAVE_LOCATION_TEST
#define SAVE_LOCATION_TEST 1
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "../src/pc/save_path.h"
#include "../src/pc/save_location.h"
#include "stubs/configfile.h" // test stub for src/pc/configfile.h
#include "../src/pc/fs/fs.h"  // real declarations; required functions faked below

// ---------------------------------------------------------------------------
// fakes for configfile / fs dependencies
// ---------------------------------------------------------------------------

char configSaveLocation[MAX_SAVE_LOCATION_STRING];

static char sConfigName[] = "sm64config.txt";
static int sConfigSaveCalls = 0;
static bool sConfigSaveFails = false;
static char sWriteRoot[1024] = "";

const char *configfile_name(void) {
    return sConfigName;
}

bool configfile_save_checked(const char *filename) {
    sConfigSaveCalls++;
    if (sConfigSaveFails)
        return false;

    char path[2048];
    char value[MAX_SAVE_LOCATION_STRING + 4];
    snprintf(path, sizeof(path), "%s/%s", sWriteRoot, filename);
    save_path_format_config_value(value, sizeof(value), configSaveLocation);
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return false;
    bool ok = fprintf(file, "save_location %s\n", value) >= 0;
    return fclose(file) == 0 && ok;
}

void configfile_save(const char *filename) {
    (void) configfile_save_checked(filename);
}

const char *fs_get_write_path(const char *vpath) {
    static char buf[2048];
    snprintf(buf, sizeof(buf), "%s/%s", sWriteRoot, vpath);
    return buf;
}

bool fs_sys_file_exists(const char *name) {
    struct stat st;
    return (stat(name, &st) == 0 && S_ISREG(st.st_mode));
}

bool fs_sys_dir_exists(const char *name) {
    struct stat st;
    return (stat(name, &st) == 0 && S_ISDIR(st.st_mode));
}

bool fs_sys_mkdir(const char *name) {
    return mkdir(name, 511) == 0;
}

uint64_t fs_sys_get_modified_time(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return (uint64_t) st.st_mtime;
}

// ---------------------------------------------------------------------------
// test utilities
// ---------------------------------------------------------------------------

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        sChecks++;                                                                                     \
        if (!(cond)) {                                                                                 \
            sFailures++;                                                                               \
            printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);                                     \
        }                                                                                              \
    } while (0)

#define CHECK_STR_EQ(got, want)                                                                        \
    do {                                                                                               \
        sChecks++;                                                                                     \
        if (strcmp((got), (want)) != 0) {                                                              \
            sFailures++;                                                                               \
            printf("FAIL %s:%d: got \"%s\" want \"%s\"\n", __func__, __LINE__, (got), (want));         \
        }                                                                                              \
    } while (0)

static void test_reset_state(void) {
    configSaveLocation[0] = '\0';
    sConfigSaveCalls = 0;
    sConfigSaveFails = false;
    sWriteRoot[0] = '\0';
    save_location_test_set_fault(SAVE_LOCATION_TEST_FAULT_NONE);
    save_location_test_set_before_publish(NULL);
    save_location_test_set_renameat2_errno(0);
}

// creates a fresh temp directory to use as the fake user (write) dir
static void test_new_root(void) {
    char tmpl[] = "/tmp/sm64sl_test_XXXXXX";
    if (mkdtemp(tmpl) == NULL) {
        perror("mkdtemp");
        exit(2);
    }
    snprintf(sWriteRoot, sizeof(sWriteRoot), "%s", tmpl);
}

static void rm_rf(const char *path) {
    DIR *dir = opendir(path);
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            char sub[2048];
            snprintf(sub, sizeof(sub), "%s/%s", path, ent->d_name);
            struct stat st;
            if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
                rm_rf(sub);
            } else {
                remove(sub);
            }
        }
        closedir(dir);
    }
    rmdir(path);
}

static void test_free_root(void) {
    if (sWriteRoot[0]) {
        rm_rf(sWriteRoot);
    }
    sWriteRoot[0] = '\0';
}

static void write_file_bytes(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(2);
    }
    if (fwrite(data, 1, len, f) != len) {
        perror("fwrite");
        exit(2);
    }
    fclose(f);
}

#define DEFAULT_SAVE_CONTENT "DEFAULT-SAVE-DATA-0123456789"

static void make_default_save(const char *content) {
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", sWriteRoot, SAVE_FILENAME);
    write_file_bytes(path, content, strlen(content));
}

static void make_custom_save(const char *dir, const char *content) {
    char path[2048];
    mkdir(dir, 511);
    snprintf(path, sizeof(path), "%s/%s", dir, SAVE_FILENAME);
    write_file_bytes(path, content, strlen(content));
}

static bool file_has_content(const char *path, const char *content) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    char buf[1024] = { 0 };
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    return n == strlen(content) && memcmp(buf, content, n) == 0;
}

static bool config_file_has_location(const char *path, const char *location) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    char line[1024] = { 0 };
    char parsed[MAX_SAVE_LOCATION_STRING];
    bool ok = fgets(line, sizeof(line), file) != NULL && fclose(file) == 0;
    return ok && save_path_parse_config_value(parsed, sizeof(parsed), line)
           && strcmp(parsed, location) == 0;
}

static void custom_save_path(char *dst, size_t dstsize, const char *dir) {
    snprintf(dst, dstsize, "%s/%s", dir, SAVE_FILENAME);
}

static void set_config_location(const char *dir) {
    size_t len = strlen(dir);
    if (len >= sizeof(configSaveLocation)) {
        fprintf(stderr, "test path too long\n");
        exit(2);
    }
    memcpy(configSaveLocation, dir, len + 1);
}

// counts leftover temporary siblings in a directory (atomic-copy cleanup check)
static int count_tmp_files(const char *dir) {
    DIR *d = opendir(dir);
    if (d == NULL) {
        return -1;
    }
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strstr(ent->d_name, ".tmp") != NULL) {
            count++;
        }
    }
    closedir(d);
    return count;
}

static void set_file_mtime(const char *path, time_t mtime) {
    struct timeval tv[2];
    tv[0].tv_sec = mtime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = mtime;
    tv[1].tv_usec = 0;
    utimes(path, tv);
}

static const char *sRaceContent;
static void create_racing_target(const char *dst) {
    write_file_bytes(dst, sRaceContent, strlen(sRaceContent));
}

// ---------------------------------------------------------------------------
// tests
// ---------------------------------------------------------------------------

static void test_validate_absolute_paths(void) {
    CHECK(save_path_validate("/storage/emulated/0/Emulation/Saves", MAX_SAVE_LOCATION_STRING)
          == SAVE_PATH_VALID);
    CHECK(save_path_validate("/", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_VALID);
    CHECK(save_path_validate("C:\\Users\\me\\Saves", MAX_SAVE_LOCATION_STRING)
          == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("C:/Users/me/Saves", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("\\\\server\\share\\saves", MAX_SAVE_LOCATION_STRING)
          == SAVE_PATH_ERR_RELATIVE);

    CHECK(save_path_validate("", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_EMPTY);
    CHECK(save_path_validate("relative/path", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("./relative", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("C:saves", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("~saves", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_RELATIVE);

    // Pure Windows path helpers remain testable on a POSIX host. Roots must
    // never be handed to mkdir as relative "C:" or partial UNC components.
    CHECK(save_path_root_length("C:\\Users\\me", true) == 3);
    CHECK(save_path_root_length("D:/Games", true) == 3);
    CHECK(save_path_root_length("\\\\server\\share\\saves", true) == strlen("\\\\server\\share\\"));
    CHECK(save_path_root_length("/tmp/saves", false) == 1);
    CHECK(save_path_root_length("C:\\relative", false) == 0);

    CHECK(save_path_validate("/a\nb", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_BAD_CHAR);
    CHECK(save_path_validate("/a\tb", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_BAD_CHAR);
    CHECK(save_path_validate("/a\rb", MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_BAD_CHAR);

    char longpath[MAX_SAVE_LOCATION_STRING + 8];
    memset(longpath, 'a', sizeof(longpath) - 1);
    longpath[0] = '/';
    longpath[sizeof(longpath) - 1] = '\0';
    CHECK(save_path_validate(longpath, MAX_SAVE_LOCATION_STRING) == SAVE_PATH_ERR_TOO_LONG);
    longpath[MAX_SAVE_LOCATION_STRING - 1] = '\0'; // exactly maxlen-1 chars fits
    CHECK(save_path_validate(longpath, MAX_SAVE_LOCATION_STRING) == SAVE_PATH_VALID);
}

static void test_config_value_spaces_roundtrip(void) {
    char formatted[MAX_SAVE_LOCATION_STRING + 4];
    char parsed[MAX_SAVE_LOCATION_STRING];
    char line[MAX_SAVE_LOCATION_STRING + 32];

    // paths without spaces are written unquoted (backwards compatible)
    save_path_format_config_value(formatted, sizeof(formatted), "/storage/emulated/0/saves");
    CHECK_STR_EQ(formatted, "/storage/emulated/0/saves");

    // paths with spaces are written quoted
    save_path_format_config_value(formatted, sizeof(formatted), "/storage/emulated/0/My Saves/sm64");
    CHECK_STR_EQ(formatted, "\"/storage/emulated/0/My Saves/sm64\"");

    // empty value formats to empty
    save_path_format_config_value(formatted, sizeof(formatted), "");
    CHECK_STR_EQ(formatted, "");

    // unquoted legacy line parses as-is
    CHECK(save_path_parse_config_value(parsed, sizeof(parsed), "save_location /a/b/c"));
    CHECK_STR_EQ(parsed, "/a/b/c");

    // quoted line parses with spaces (and doubled spaces preserved)
    CHECK(save_path_parse_config_value(parsed, sizeof(parsed), "save_location \"/a/My Saves  dir\""));
    CHECK_STR_EQ(parsed, "/a/My Saves  dir");

    // surrounding whitespace is trimmed
    CHECK(save_path_parse_config_value(parsed, sizeof(parsed), "   save_location   /a/b   "));
    CHECK_STR_EQ(parsed, "/a/b");

    // valueless line means "empty" (stock behavior)
    CHECK(save_path_parse_config_value(parsed, sizeof(parsed), "save_location"));
    CHECK_STR_EQ(parsed, "");

    // keys that merely start with the name must not match
    CHECK(!save_path_parse_config_value(parsed, sizeof(parsed), "save_location_extra /a"));
    CHECK(!save_path_parse_config_value(parsed, sizeof(parsed), "other_option /a"));
    CHECK(!save_path_parse_config_value(parsed, sizeof(parsed), "#save_location /a"));

    // round trip: format then parse restores the exact value
    const char *values[] = {
        "/storage/emulated/0/Emulation/Saves/sm64coopdx",
        "/home/user/My Saves/sm64 coopdx",
        "/a/b  c", // doubled internal spaces must survive
        "D:\\Games\\My Saves\\sm64",
        "",
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        save_path_format_config_value(formatted, sizeof(formatted), values[i]);
        snprintf(line, sizeof(line), "save_location %s", formatted);
        CHECK(save_path_parse_config_value(parsed, sizeof(parsed), line));
        CHECK_STR_EQ(parsed, values[i]);
    }
}

static void test_switch_default_to_custom(void) {
    test_reset_state();
    test_new_root();
    make_default_save(DEFAULT_SAVE_CONTENT);
    char dir[2048];
    snprintf(dir, sizeof(dir), "%s/custom saves A", sWriteRoot); // note the space

    CHECK(save_location_set(dir) == SAVE_LOCATION_OK);
    CHECK_STR_EQ(configSaveLocation, dir);
    CHECK(sConfigSaveCalls == 1); // config persisted immediately

    char savePath[2048];
    custom_save_path(savePath, sizeof(savePath), dir);
    CHECK(file_has_content(savePath, DEFAULT_SAVE_CONTENT));     // migrated copy
    CHECK(fs_sys_file_exists(fs_get_write_path(SAVE_FILENAME))); // default never deleted

    // switching again to another dir with a missing target copies the current save
    char dirB[2048];
    snprintf(dirB, sizeof(dirB), "%s/dirB", sWriteRoot);
    CHECK(save_location_set(dirB) == SAVE_LOCATION_OK);
    CHECK_STR_EQ(configSaveLocation, dirB);
    custom_save_path(savePath, sizeof(savePath), dirB);
    CHECK(file_has_content(savePath, DEFAULT_SAVE_CONTENT));
    custom_save_path(savePath, sizeof(savePath), dir); // old custom save kept as fallback
    CHECK(file_has_content(savePath, DEFAULT_SAVE_CONTENT));
    CHECK(sConfigSaveCalls == 2);

    test_free_root();
}

static void test_switch_existing_target_preserved(void) {
    test_reset_state();
    test_new_root();
    char dirA[2048], dirB[2048];
    snprintf(dirA, sizeof(dirA), "%s/dirA", sWriteRoot);
    snprintf(dirB, sizeof(dirB), "%s/dirB", sWriteRoot);
    make_custom_save(dirA, "SAVE-A");
    make_custom_save(dirB, "SAVE-B-EXISTING"); // target already has a save

    CHECK(save_location_set(dirA) == SAVE_LOCATION_OK);
    CHECK(save_location_set(dirB) == SAVE_LOCATION_OK);

    char savePath[2048];
    custom_save_path(savePath, sizeof(savePath), dirB);
    CHECK(file_has_content(savePath, "SAVE-B-EXISTING")); // never overwritten
    custom_save_path(savePath, sizeof(savePath), dirA);
    CHECK(file_has_content(savePath, "SAVE-A")); // source untouched

    test_free_root();
}

static void test_switch_failures_preserve_config(void) {
    test_reset_state();
    test_new_root();
    make_default_save(DEFAULT_SAVE_CONTENT);
    char dirA[2048];
    snprintf(dirA, sizeof(dirA), "%s/dirA", sWriteRoot);
    make_custom_save(dirA, "SAVE-A");
    CHECK(save_location_set(dirA) == SAVE_LOCATION_OK);
    int savesBefore = sConfigSaveCalls;

    // relative / empty / bad-char / overlong values are rejected without touching config
    CHECK(save_location_set("relative/dir") == SAVE_LOCATION_ERR_RELATIVE);
    CHECK(save_location_set("C:\\sm64coopdx-test") == SAVE_LOCATION_ERR_RELATIVE);
    CHECK(!fs_sys_dir_exists("C:\\sm64coopdx-test"));
    CHECK(save_location_set("") == SAVE_LOCATION_ERR_EMPTY);
    CHECK(save_location_set("/a\nb") == SAVE_LOCATION_ERR_BAD_CHAR);
    char longpath[MAX_SAVE_LOCATION_STRING + 8];
    memset(longpath, 'a', sizeof(longpath) - 1);
    longpath[0] = '/';
    longpath[sizeof(longpath) - 1] = '\0';
    CHECK(save_location_set(longpath) == SAVE_LOCATION_ERR_TOO_LONG);
    CHECK_STR_EQ(configSaveLocation, dirA);
    CHECK(sConfigSaveCalls == savesBefore); // nothing persisted on failure

    // directory creation failure: a file blocks the path
    char blocker[2048];
    snprintf(blocker, sizeof(blocker), "%s/blocker", sWriteRoot);
    write_file_bytes(blocker, "x", 1);
    char under[2048];
    snprintf(under, sizeof(under), "%s/blocker/sub", sWriteRoot);
    CHECK(save_location_set(under) == SAVE_LOCATION_ERR_MKDIR);
    CHECK_STR_EQ(configSaveLocation, dirA);

    // a stale legacy temp name cannot block a copy: each operation uses a
    // unique exclusively-created sibling and cleans only its own temp.
    char dirT[2048];
    snprintf(dirT, sizeof(dirT), "%s/dirT", sWriteRoot);
    mkdir(dirT, 511);
    char tmpSquat[4096];
    snprintf(tmpSquat, sizeof(tmpSquat), "%s/%s.tmp", dirT, SAVE_FILENAME);
    mkdir(tmpSquat, 511);
    CHECK(save_location_set(dirT) == SAVE_LOCATION_OK);
    CHECK_STR_EQ(configSaveLocation, dirT);
    char dstPath[2048];
    custom_save_path(dstPath, sizeof(dstPath), dirT);
    CHECK(file_has_content(dstPath, "SAVE-A"));
    CHECK(count_tmp_files(dirT) == 1); // only the pre-existing squatter remains

    CHECK(save_location_set(dirA) == SAVE_LOCATION_OK);

    // rename failure: destination path exists as a directory
    char dirR[2048];
    snprintf(dirR, sizeof(dirR), "%s/dirR", sWriteRoot);
    mkdir(dirR, 511);
    char dstDir[2048];
    custom_save_path(dstDir, sizeof(dstDir), dirR);
    mkdir(dstDir, 511);
    CHECK(save_location_set(dirR) == SAVE_LOCATION_ERR_COPY);
    CHECK_STR_EQ(configSaveLocation, dirA);
    CHECK(count_tmp_files(dirR) == 0); // temp cleaned up
    CHECK(fs_sys_dir_exists(dstDir));  // squatter untouched

    // the previously active save is still selected by resolve()
    char resolved[2048];
    snprintf(resolved, sizeof(resolved), "%s", save_location_resolve());
    char expect[2048];
    custom_save_path(expect, sizeof(expect), dirA);
    CHECK_STR_EQ(resolved, expect);

    test_free_root();
}

static void test_reset_persists_and_copies_back(void) {
    test_reset_state();
    test_new_root();
    make_default_save(DEFAULT_SAVE_CONTENT);
    char dirA[2048];
    snprintf(dirA, sizeof(dirA), "%s/custom A", sWriteRoot);
    CHECK(save_location_set(dirA) == SAVE_LOCATION_OK);

    // overwrite the custom save with newer content
    char customPath[2048];
    custom_save_path(customPath, sizeof(customPath), dirA);
    write_file_bytes(customPath, "NEWER-CUSTOM", strlen("NEWER-CUSTOM"));
    // make the default save clearly older
    char defaultPath[2048];
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);
    set_file_mtime(defaultPath, time(NULL) - 10000);

    int savesBefore = sConfigSaveCalls;
    CHECK(save_location_reset_to_default() == SAVE_LOCATION_OK);
    CHECK_STR_EQ(configSaveLocation, "");                 // cleared
    CHECK(sConfigSaveCalls == savesBefore + 1);           // cleared config persisted
    CHECK(file_has_content(defaultPath, "NEWER-CUSTOM")); // lossless copy-back
    CHECK(file_has_content(customPath, "NEWER-CUSTOM"));  // custom copy never deleted
    CHECK_STR_EQ(save_location_resolve(), defaultPath);   // default selected again

    // resetting while already on the default is a no-op success
    savesBefore = sConfigSaveCalls;
    CHECK(save_location_reset_to_default() == SAVE_LOCATION_OK);
    CHECK(sConfigSaveCalls == savesBefore);

    test_free_root();
}

static void test_reset_preserves_newer_default(void) {
    test_reset_state();
    test_new_root();
    make_default_save("NEWER-DEFAULT");
    char defaultPath[2048];
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);
    set_file_mtime(defaultPath, time(NULL) + 10000); // default is newer

    char dirA[2048];
    snprintf(dirA, sizeof(dirA), "%s/dirA", sWriteRoot);
    make_custom_save(dirA, "OLDER-CUSTOM");
    CHECK(save_location_set(dirA) == SAVE_LOCATION_OK);

    CHECK(save_location_reset_to_default() == SAVE_LOCATION_OK);
    CHECK(file_has_content(defaultPath, "NEWER-DEFAULT")); // never clobbered
    CHECK_STR_EQ(configSaveLocation, "");

    test_free_root();
}

static void test_reset_failure_keeps_custom(void) {
    test_reset_state();
    test_new_root();
    char dirA[2048];
    snprintf(dirA, sizeof(dirA), "%s/dirA", sWriteRoot);
    make_custom_save(dirA, "CUSTOM-DATA");
    CHECK(save_location_set(dirA) == SAVE_LOCATION_OK);

    // squat a directory on the default save path so copy-back cannot land
    char defaultPath[2048];
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);
    mkdir(defaultPath, 511);

    int savesBefore = sConfigSaveCalls;
    CHECK(save_location_reset_to_default() == SAVE_LOCATION_ERR_COPY);
    CHECK_STR_EQ(configSaveLocation, dirA);  // custom path stays active
    CHECK(sConfigSaveCalls == savesBefore);  // nothing persisted
    CHECK(count_tmp_files(sWriteRoot) == 0); // temp cleaned up
    char customPath[2048];
    custom_save_path(customPath, sizeof(customPath), dirA);
    CHECK(file_has_content(customPath, "CUSTOM-DATA"));

    test_free_root();
}

static void test_reset_equal_mtime_conflicts(void) {
    test_reset_state();
    test_new_root();
    make_default_save("SAME-BYTES");
    char defaultPath[2048], dir[2048], customPath[2048];
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);
    snprintf(dir, sizeof(dir), "%s/custom", sWriteRoot);
    make_custom_save(dir, "SAME-BYTES");
    custom_save_path(customPath, sizeof(customPath), dir);
    set_file_mtime(defaultPath, 123456);
    set_file_mtime(customPath, 123456);
    set_config_location(dir);

    CHECK(save_location_reset_to_default() == SAVE_LOCATION_OK);
    CHECK_STR_EQ(configSaveLocation, "");

    set_config_location(dir);
    write_file_bytes(customPath, "DIFFERENT", strlen("DIFFERENT"));
    set_file_mtime(defaultPath, 123456);
    set_file_mtime(customPath, 123456);
    int savesBefore = sConfigSaveCalls;
    // equal mtime + differing bytes is a content conflict, not a copy-back
    // failure: it gets its own result so the UI can explain the manual choice
    CHECK(save_location_reset_to_default() == SAVE_LOCATION_ERR_CONFLICT);
    CHECK_STR_EQ(configSaveLocation, dir);
    CHECK(file_has_content(defaultPath, "SAME-BYTES"));
    CHECK(sConfigSaveCalls == savesBefore);
    test_free_root();
}

static void test_no_clobber_publish_race(void) {
    test_reset_state();
    test_new_root();
    make_default_save("SOURCE");
    char dir[2048], dst[2048];
    snprintf(dir, sizeof(dir), "%s/race", sWriteRoot);
    sRaceContent = "RACING-WINNER";
    save_location_test_set_before_publish(create_racing_target);

    CHECK(save_location_set(dir) == SAVE_LOCATION_OK);
    custom_save_path(dst, sizeof(dst), dir);
    CHECK(file_has_content(dst, "RACING-WINNER"));
    CHECK_STR_EQ(configSaveLocation, dir);
    CHECK(count_tmp_files(dir) == 0);
    save_location_test_set_before_publish(NULL);
    test_free_root();
}

// The publication must prefer renameat2(RENAME_NOREPLACE): it is the only
// atomic no-clobber primitive on filesystems that reject hard links (Android
// /storage/emulated/0 FUSE). On other POSIX platforms the link fallback wins.
static void test_publish_prefers_renameat2(void) {
    test_reset_state();
    test_new_root();
    make_default_save("RENAME-PRIMARY");
    char dir[2048], dst[2048];
    snprintf(dir, sizeof(dir), "%s/primary", sWriteRoot);

    CHECK(save_location_set(dir) == SAVE_LOCATION_OK);
#if defined(__linux__)
    CHECK(save_location_test_last_publish() == SAVE_LOCATION_TEST_PUBLISH_RENAMEAT2);
#else
    CHECK(save_location_test_last_publish() == SAVE_LOCATION_TEST_PUBLISH_LINK);
#endif
    custom_save_path(dst, sizeof(dst), dir);
    CHECK(file_has_content(dst, "RENAME-PRIMARY"));
    CHECK(count_tmp_files(dir) == 0); // temp consumed by the rename
    test_free_root();
}

// Where renameat2 reports "unsupported" (old kernel/filesystem, or a libc
// without the wrapper), publication must still succeed through link(2), which
// never replaces an existing destination.
static void test_publish_renameat2_unsupported_falls_back_to_link(void) {
    const int unsupported[] = { ENOSYS, EINVAL, EOPNOTSUPP };
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
        test_reset_state();
        test_new_root();
        make_default_save("FALLBACK-SOURCE");
        char dir[2048], dst[2048];
        snprintf(dir, sizeof(dir), "%s/fallback-%zu", sWriteRoot, i);
        save_location_test_set_renameat2_errno(unsupported[i]);

        CHECK(save_location_set(dir) == SAVE_LOCATION_OK);
        CHECK(save_location_test_last_publish() == SAVE_LOCATION_TEST_PUBLISH_LINK);
        custom_save_path(dst, sizeof(dst), dir);
        CHECK(file_has_content(dst, "FALLBACK-SOURCE"));
        CHECK(count_tmp_files(dir) == 0); // link published, temp removed
        test_free_root();
    }
}

// renameat2 EEXIST must preserve an existing regular destination (a winning
// racing writer) while a non-file squatter on the same errno is an error.
static void test_publish_renameat2_eexist_classification(void) {
    // regular file at the destination: the racing writer wins, publish reports
    // EXISTS and nothing overwrites it
    test_reset_state();
    test_new_root();
    make_default_save("SOURCE");
    char dir[2048], dst[2048];
    snprintf(dir, sizeof(dir), "%s/eexist-file", sWriteRoot);
    sRaceContent = "RACING-WINNER";
    save_location_test_set_before_publish(create_racing_target);
    save_location_test_set_renameat2_errno(EEXIST);

    CHECK(save_location_set(dir) == SAVE_LOCATION_OK);
    CHECK(save_location_test_last_publish() == SAVE_LOCATION_TEST_PUBLISH_NONE);
    custom_save_path(dst, sizeof(dst), dir);
    CHECK(file_has_content(dst, "RACING-WINNER")); // racing writer preserved
    CHECK(count_tmp_files(dir) == 0);               // temp cleaned up
    save_location_test_set_before_publish(NULL);
    test_free_root();

    // directory at the destination: same errno, but unusable -> no publication
    test_reset_state();
    test_new_root();
    make_default_save("SOURCE");
    snprintf(dir, sizeof(dir), "%s/eexist-dir", sWriteRoot);
    mkdir(dir, 511);
    custom_save_path(dst, sizeof(dst), dir);
    mkdir(dst, 511); // directory squats the destination
    set_config_location(dir);
    save_location_test_set_renameat2_errno(EEXIST);

    char resolved[2048];
    snprintf(resolved, sizeof(resolved), "%s", save_location_resolve());
    char defaultPath[2048];
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);
    CHECK_STR_EQ(resolved, defaultPath); // failed publish falls back to default
    CHECK(fs_sys_dir_exists(dst));      // squatter untouched
    CHECK(count_tmp_files(dir) == 0);    // temp cleaned up
    test_free_root();
}

// Any other renameat2 errno (permissions, I/O) must fail safely: nothing is
// published, nothing is clobbered, and the temp is cleaned up.
static void test_publish_renameat2_hard_error_fails_safe(void) {
    test_reset_state();
    test_new_root();
    make_default_save("SOURCE");
    char dir[2048], dst[2048];
    snprintf(dir, sizeof(dir), "%s/hard-error", sWriteRoot);
    save_location_test_set_renameat2_errno(EACCES);

    CHECK(save_location_set(dir) == SAVE_LOCATION_ERR_COPY);
    custom_save_path(dst, sizeof(dst), dir);
    CHECK(!fs_sys_file_exists(dst)); // nothing published
    CHECK(count_tmp_files(dir) == 0); // temp cleaned up
    CHECK(save_location_test_last_publish() == SAVE_LOCATION_TEST_PUBLISH_NONE);
    CHECK_STR_EQ(configSaveLocation, "");
    test_free_root();
}

// The host join snapshot (packet_join.c) reads through this helper, so it must
// return the active custom save's bytes -- not the default directory's -- with
// the legacy short-read/missing-file semantics preserved.
static void test_read_eeprom_uses_active_save(void) {
    test_reset_state();
    test_new_root();

    // default location active: reads the default save
    make_default_save(DEFAULT_SAVE_CONTENT);
    unsigned char buf[512];
    memset(buf, 0xAA, sizeof(buf));
    size_t got = save_location_read_eeprom(buf, sizeof(buf));
    CHECK(got == strlen(DEFAULT_SAVE_CONTENT));
    CHECK(memcmp(buf, DEFAULT_SAVE_CONTENT, strlen(DEFAULT_SAVE_CONTENT)) == 0);

    // custom location active: must read the custom save (join snapshot fix)
    char dir[2048], dst[2048];
    snprintf(dir, sizeof(dir), "%s/custom", sWriteRoot);
    make_custom_save(dir, "CUSTOM-EEPROM-SNAPSHOT");
    set_config_location(dir);
    memset(buf, 0xAA, sizeof(buf));
    got = save_location_read_eeprom(buf, sizeof(buf));
    CHECK(got == strlen("CUSTOM-EEPROM-SNAPSHOT"));
    CHECK(memcmp(buf, "CUSTOM-EEPROM-SNAPSHOT", strlen("CUSTOM-EEPROM-SNAPSHOT")) == 0);

    // exact 512-byte read for a full-size save
    static const unsigned char full[512] = { 1, 2, 3 };
    custom_save_path(dst, sizeof(dst), dir);
    write_file_bytes(dst, full, sizeof(full));
    memset(buf, 0xAA, sizeof(buf));
    got = save_location_read_eeprom(buf, sizeof(buf));
    CHECK(got == 512);
    CHECK(memcmp(buf, full, 512) == 0);

    // missing active save: zero bytes and the buffer stays untouched
    test_free_root();
    test_new_root();
    memset(buf, 0xAA, sizeof(buf));
    got = save_location_read_eeprom(buf, sizeof(buf));
    CHECK(got == 0);
    CHECK(buf[0] == 0xAA && buf[511] == 0xAA);
    test_free_root();
}

static void test_copy_durability_failures_do_not_publish(void) {
    const enum save_location_test_fault faults[] = {
        SAVE_LOCATION_TEST_FAULT_FLUSH,
        SAVE_LOCATION_TEST_FAULT_SYNC,
        SAVE_LOCATION_TEST_FAULT_CLOSE,
        SAVE_LOCATION_TEST_FAULT_PUBLISH,
    };
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
        test_reset_state();
        test_new_root();
        make_default_save("SOURCE");
        char dir[2048], dst[2048];
        snprintf(dir, sizeof(dir), "%s/fault-%u", sWriteRoot, (unsigned) i);
        save_location_test_set_fault(faults[i]);
        CHECK(save_location_set(dir) == SAVE_LOCATION_ERR_COPY);
        custom_save_path(dst, sizeof(dst), dir);
        CHECK(!fs_sys_file_exists(dst));
        CHECK(count_tmp_files(dir) == 0);
        CHECK_STR_EQ(configSaveLocation, "");
        save_location_test_set_fault(SAVE_LOCATION_TEST_FAULT_NONE);
        test_free_root();
    }
}

static void test_boot_migration_failure_falls_back_and_retries(void) {
    test_reset_state();
    test_new_root();
    make_default_save("DEFAULT");
    char dir[2048], customPath[2048], defaultPath[2048];
    snprintf(dir, sizeof(dir), "%s/custom", sWriteRoot);
    set_config_location(dir);
    custom_save_path(customPath, sizeof(customPath), dir);
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);

    save_location_test_set_fault(SAVE_LOCATION_TEST_FAULT_PUBLISH);
    CHECK_STR_EQ(save_location_resolve(), defaultPath);
    CHECK(!fs_sys_file_exists(customPath));
    save_location_test_set_fault(SAVE_LOCATION_TEST_FAULT_NONE);
    CHECK_STR_EQ(save_location_resolve(), customPath);
    CHECK(file_has_content(customPath, "DEFAULT"));
    test_free_root();
}

static void test_boot_migration_directory_target_falls_back(void) {
    test_reset_state();
    test_new_root();
    make_default_save("DEFAULT");
    char dir[2048], customPath[2048], defaultPath[2048];
    snprintf(dir, sizeof(dir), "%s/custom", sWriteRoot);
    mkdir(dir, 511);
    custom_save_path(customPath, sizeof(customPath), dir);
    mkdir(customPath, 511); // a directory squats the custom save path
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);
    set_config_location(dir);

    // EEXIST on a non-file target must not be mistaken for a winning racing
    // writer: the failed migration returns NULL so the default stays selected.
    CHECK_STR_EQ(save_location_resolve(), defaultPath);
    CHECK(fs_sys_dir_exists(customPath)); // squatter untouched
    CHECK(count_tmp_files(dir) == 0);      // temp cleaned up
    test_free_root();
}

static void test_config_persistence_failure_restores_active_path(void) {
    test_reset_state();
    test_new_root();
    make_default_save("DEFAULT");
    char dirA[2048], dirB[2048], configPath[2048];
    snprintf(dirA, sizeof(dirA), "%s/A", sWriteRoot);
    snprintf(dirB, sizeof(dirB), "%s/B with space", sWriteRoot);
    CHECK(save_location_set(dirA) == SAVE_LOCATION_OK);
    snprintf(configPath, sizeof(configPath), "%s/%s", sWriteRoot, sConfigName);
    CHECK(config_file_has_location(configPath, dirA));

    sConfigSaveFails = true;
    CHECK(save_location_set(dirB) == SAVE_LOCATION_ERR_CONFIG);
    CHECK_STR_EQ(configSaveLocation, dirA);
    CHECK(config_file_has_location(configPath, dirA));
    char copiedB[2048];
    custom_save_path(copiedB, sizeof(copiedB), dirB);
    CHECK(file_has_content(copiedB, "DEFAULT"));
    char expectedA[2048];
    custom_save_path(expectedA, sizeof(expectedA), dirA);
    CHECK_STR_EQ(save_location_resolve(), expectedA);

    sConfigSaveFails = false;
    CHECK(save_location_set(dirB) == SAVE_LOCATION_OK);
    CHECK(config_file_has_location(configPath, dirB));

    sConfigSaveFails = true;
    CHECK(save_location_reset_to_default() == SAVE_LOCATION_ERR_CONFIG);
    CHECK_STR_EQ(configSaveLocation, dirB);
    test_free_root();
}

static void test_resolve_boot_migration_and_fallback(void) {
    test_reset_state();
    test_new_root();
    make_default_save(DEFAULT_SAVE_CONTENT);

    // configured custom dir with a space migrates the default save atomically on resolve
    char dir[2048];
    snprintf(dir, sizeof(dir), "%s/My Saves Dir", sWriteRoot);
    set_config_location(dir);
    const char *resolved = save_location_resolve();
    char expect[2048];
    custom_save_path(expect, sizeof(expect), dir);
    CHECK_STR_EQ(resolved, expect);
    CHECK(file_has_content(expect, DEFAULT_SAVE_CONTENT));
    CHECK(count_tmp_files(dir) == 0);

    // unusable custom dir falls back to the default save path without crashing
    char blocker[2048];
    snprintf(blocker, sizeof(blocker), "%s/blocker", sWriteRoot);
    write_file_bytes(blocker, "x", 1);
    char blockedConfig[2048];
    snprintf(blockedConfig, sizeof(blockedConfig), "%s/blocker/sub", sWriteRoot);
    set_config_location(blockedConfig);
    resolved = save_location_resolve();
    char defaultPath[2048];
    snprintf(defaultPath, sizeof(defaultPath), "%s/%s", sWriteRoot, SAVE_FILENAME);
    CHECK_STR_EQ(resolved, defaultPath);

    // existing custom save is never re-migrated / overwritten by the default
    set_config_location(dir);
    write_file_bytes(expect, "CUSTOM-ONLY", strlen("CUSTOM-ONLY"));
    resolved = save_location_resolve();
    CHECK_STR_EQ(resolved, expect);
    CHECK(file_has_content(expect, "CUSTOM-ONLY"));

    test_free_root();
}

int main(void) {
    test_validate_absolute_paths();
    test_config_value_spaces_roundtrip();
    test_switch_default_to_custom();
    test_switch_existing_target_preserved();
    test_switch_failures_preserve_config();
    test_reset_persists_and_copies_back();
    test_reset_preserves_newer_default();
    test_reset_failure_keeps_custom();
    test_reset_equal_mtime_conflicts();
    test_no_clobber_publish_race();
    test_publish_prefers_renameat2();
    test_publish_renameat2_unsupported_falls_back_to_link();
    test_publish_renameat2_eexist_classification();
    test_publish_renameat2_hard_error_fails_safe();
    test_read_eeprom_uses_active_save();
    test_copy_durability_failures_do_not_publish();
    test_boot_migration_failure_falls_back_and_retries();
    test_boot_migration_directory_target_falls_back();
    test_config_persistence_failure_restores_active_path();
    test_resolve_boot_migration_and_fallback();

    printf("%d checks, %d failures\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
