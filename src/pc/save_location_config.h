#ifndef SM64_SAVE_LOCATION_CONFIG_H_
#define SM64_SAVE_LOCATION_CONFIG_H_

#include <stdbool.h>

#define MAX_SAVE_LOCATION_STRING 256

extern char configSaveLocation[MAX_SAVE_LOCATION_STRING];
bool configfile_save_checked(const char *filename);
const char *configfile_name(void);

#endif // SM64_SAVE_LOCATION_CONFIG_H_
