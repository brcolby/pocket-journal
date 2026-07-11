#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_HOME_MAX_SLOTS 4
#define PJ_HOME_TITLE_LEN 25
#define PJ_HOME_LABEL_LEN 13
#define PJ_HOME_ICON_LEN 17
#define PJ_HOME_DESTINATION_LEN 17
#define PJ_HOME_RECORD_BYTES 228

typedef struct {
    char label[PJ_HOME_LABEL_LEN];
    char icon[PJ_HOME_ICON_LEN];
    char destination[PJ_HOME_DESTINATION_LEN];
} pj_home_slot_t;

typedef struct {
    char title[PJ_HOME_TITLE_LEN];
    uint8_t slot_count;
    pj_home_slot_t slots[PJ_HOME_MAX_SLOTS];
} pj_home_layout_t;

void pj_home_layout_defaults(pj_home_layout_t *layout);
int pj_home_layout_valid(const pj_home_layout_t *layout);
int pj_home_layout_canonical_copy(pj_home_layout_t *target, const pj_home_layout_t *source);
int pj_home_icon_supported(const char *icon);
int pj_home_destination_supported(const char *destination);
size_t pj_home_layout_encode_record(const pj_home_layout_t *layout, uint8_t *record, size_t record_size);
int pj_home_layout_decode_record(const uint8_t *record, size_t record_size, pj_home_layout_t *layout);
int pj_home_layout_decode_or_default(const uint8_t *record, size_t record_size, pj_home_layout_t *layout);

#ifdef __cplusplus
}
#endif
