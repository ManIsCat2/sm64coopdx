// Test stub for src/pc/configfile.h.
// Mirrors only the declarations the save-location modules depend on; the real
// header drags in game headers that are not needed for these unit tests.
// Keep MAX_SAVE_LOCATION_STRING in sync with src/pc/configfile.h.
#ifndef TEST_STUB_CONFIGFILE_H_
#define TEST_STUB_CONFIGFILE_H_

#define MAX_SAVE_LOCATION_STRING 256

extern char configSaveLocation[MAX_SAVE_LOCATION_STRING];

void configfile_save(const char *filename);
bool configfile_save_checked(const char *filename);
const char *configfile_name(void);

#endif // TEST_STUB_CONFIGFILE_H_
