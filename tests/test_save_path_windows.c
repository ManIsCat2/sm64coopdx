#include <stdio.h>

#include "../src/pc/save_path.h"

static int failures;
#define CHECK(condition)                                                                               \
    do {                                                                                               \
        if (!(condition)) {                                                                            \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                               \
            failures++;                                                                                \
        }                                                                                              \
    } while (0)

int main(void) {
    CHECK(save_path_validate("C:\\Users\\me\\Saves", 256) == SAVE_PATH_VALID);
    CHECK(save_path_validate("D:/Games/Saves", 256) == SAVE_PATH_VALID);
    CHECK(save_path_validate("\\\\server\\share\\saves", 256) == SAVE_PATH_VALID);
    CHECK(save_path_validate("//server/share/saves", 256) == SAVE_PATH_VALID);
    CHECK(save_path_validate("C:saves", 256) == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("/posix-only", 256) == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("\\\\server", 256) == SAVE_PATH_ERR_RELATIVE);
    CHECK(save_path_validate("\\\\server\\", 256) == SAVE_PATH_ERR_RELATIVE);

    CHECK(save_path_root_length("C:\\Users", true) == 3);
    CHECK(save_path_root_length("\\\\server\\share\\dir", true) == 15);
    return failures == 0 ? 0 : 1;
}
