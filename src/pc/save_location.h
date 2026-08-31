#ifndef SM64_SAVE_LOCATION_H_
#define SM64_SAVE_LOCATION_H_

#include <stddef.h>
#include <stdbool.h>

// Resolves the absolute path EEPROM read/write must use for SAVE_FILENAME: the
// configured custom directory (configSaveLocation) when set and usable --
// creating it and performing the one-time migration as needed -- otherwise the
// default user-dir save. Never fails or crashes: falls back to the default path
// and logs a warning if the custom directory cannot be used.
const char *save_location_resolve(void);

// Reads the active save file (honoring the configured custom save location)
// into dst, at most size bytes, and returns the number of bytes read.
// Returns 0 without touching dst when the active save file is missing or
// unreadable; a short read leaves the tail of dst unchanged, mirroring the
// legacy fs_open/fs_read snapshot behavior. The host's join snapshot uses
// this so joining clients receive the host's active EEPROM.
size_t save_location_read_eeprom(void *dst, size_t size);

// Result of switching the save directory with save_location_set().
enum save_location_result {
    SAVE_LOCATION_OK = 0,
    SAVE_LOCATION_ERR_EMPTY,    // no directory given
    SAVE_LOCATION_ERR_RELATIVE, // not an absolute path
    SAVE_LOCATION_ERR_BAD_CHAR, // control characters in the path
    SAVE_LOCATION_ERR_TOO_LONG, // path does not fit the config buffers
    SAVE_LOCATION_ERR_MKDIR,    // target directory could not be created
    SAVE_LOCATION_ERR_COPY,     // save could not be copied into the target
    SAVE_LOCATION_ERR_CONFIG,   // updated config could not be persisted
    SAVE_LOCATION_ERR_CONFLICT, // both locations hold different data (reset)
};

// Switches the active save directory to `dir` (an absolute path, possibly
// containing spaces). Before changing anything it identifies the currently
// selected on-disk save, creates the target directory, and -- only when the
// target has no save yet -- copies the current save over atomically (an
// existing target save is never overwritten). Only after all of that succeeds
// does it update configSaveLocation and persist the config, so any failure
// leaves the previous config and path active. The caller is expected to reload
// the save state afterwards (e.g. save_file_reload).
enum save_location_result save_location_set(const char *dir);

#ifdef SAVE_LOCATION_TEST
enum save_location_test_fault {
    SAVE_LOCATION_TEST_FAULT_NONE = 0,
    SAVE_LOCATION_TEST_FAULT_FLUSH,
    SAVE_LOCATION_TEST_FAULT_SYNC,
    SAVE_LOCATION_TEST_FAULT_CLOSE,
    SAVE_LOCATION_TEST_FAULT_PUBLISH,
};
void save_location_test_set_fault(enum save_location_test_fault fault);
void save_location_test_set_before_publish(void (*callback)(const char *dst));

// Simulates renameat2(RENAME_NOREPLACE) failing with `error` (0 = perform the
// real syscall) so publication strategy tests are deterministic.
void save_location_test_set_renameat2_errno(int error);

// Which primitive the last no-replace publish used to publish a file.
enum save_location_test_publish {
    SAVE_LOCATION_TEST_PUBLISH_NONE = 0,
    SAVE_LOCATION_TEST_PUBLISH_RENAMEAT2,
    SAVE_LOCATION_TEST_PUBLISH_LINK,
};
enum save_location_test_publish save_location_test_last_publish(void);
#endif

// While the custom directory is still known, copies the custom save back to the
// default location only if it is missing or older. Equal timestamps require
// byte-identical files; differing bytes are treated as a conflict. It then
// clears configSaveLocation and persists the config. Returns an error while
// leaving the custom location active if copy-back or persistence fails.
enum save_location_result save_location_reset_to_default(void);

#endif // SM64_SAVE_LOCATION_H_
