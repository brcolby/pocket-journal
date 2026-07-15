#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PJ_OTA_SHA256_HEX_LEN 64U
#define PJ_OTA_TEXT_LEN 64U
#define PJ_OTA_BOARD_LEN 80U
#define PJ_OTA_UPLOAD_ID_LEN 32U
#define PJ_OTA_CANONICAL_MAX 512U

typedef struct {
    uint64_t size;
    char sha256[PJ_OTA_SHA256_HEX_LEN + 1U];
    char project[PJ_OTA_TEXT_LEN];
    char board[PJ_OTA_BOARD_LEN];
    char target[PJ_OTA_TEXT_LEN];
    char version[PJ_OTA_TEXT_LEN];
    uint32_t secure_version;
} pj_ota_manifest_t;

typedef struct {
    const char *project;
    const char *board;
    const char *target;
    const char *version;
    uint32_t secure_version;
    uint64_t partition_capacity;
} pj_ota_identity_t;

typedef enum {
    PJ_OTA_CHECK_ACCEPTED = 0,
    PJ_OTA_CHECK_BUSY,
    PJ_OTA_CHECK_KEY_UNCONFIGURED,
    PJ_OTA_CHECK_SIGNED_APP_UNCONFIGURED,
    PJ_OTA_CHECK_SIGNATURE_INVALID,
    PJ_OTA_CHECK_MANIFEST_INVALID,
    PJ_OTA_CHECK_SIZE_INVALID,
    PJ_OTA_CHECK_PROJECT_MISMATCH,
    PJ_OTA_CHECK_BOARD_MISMATCH,
    PJ_OTA_CHECK_TARGET_MISMATCH,
    PJ_OTA_CHECK_VERSION_REPLAY,
    PJ_OTA_CHECK_VERSION_DOWNGRADE,
    PJ_OTA_CHECK_SECURE_VERSION,
    PJ_OTA_CHECK_IMAGE_INVALID,
    PJ_OTA_CHECK_IMAGE_DIGEST,
    PJ_OTA_CHECK_IMAGE_DESCRIPTOR,
    PJ_OTA_CHECK_PARTITION_ACTIVE,
} pj_ota_check_t;

const char *pj_ota_check_code(pj_ota_check_t result);
int pj_ota_manifest_valid(const pj_ota_manifest_t *manifest);
int pj_ota_manifest_canonicalize(const pj_ota_manifest_t *manifest,
                                 char *output,
                                 size_t output_size,
                                 size_t *written);
pj_ota_check_t pj_ota_preflight_check(const pj_ota_manifest_t *manifest,
                                      const pj_ota_identity_t *identity,
                                      int verification_key_configured,
                                      int signed_app_verification_configured,
                                      int signature_verified,
                                      int session_active);
pj_ota_check_t pj_ota_image_check(const pj_ota_manifest_t *manifest,
                                  const char *image_project,
                                  const char *image_version,
                                  uint32_t image_secure_version,
                                  int digest_matches,
                                  int image_verified,
                                  int target_verified,
                                  int partition_is_inactive);

typedef enum {
    PJ_OTA_TRANSFER_IDLE = 0,
    PJ_OTA_TRANSFER_READY,
    PJ_OTA_TRANSFER_WRITING,
    PJ_OTA_TRANSFER_PENDING_REBOOT,
    PJ_OTA_TRANSFER_FAILED,
} pj_ota_transfer_state_t;

typedef enum {
    PJ_OTA_TRANSFER_OK = 0,
    PJ_OTA_TRANSFER_BUSY,
    PJ_OTA_TRANSFER_REPLAY,
    PJ_OTA_TRANSFER_ID_MISMATCH,
    PJ_OTA_TRANSFER_OFFSET_MISMATCH,
    PJ_OTA_TRANSFER_OVERFLOW,
    PJ_OTA_TRANSFER_INCOMPLETE,
    PJ_OTA_TRANSFER_INVALID_STATE,
} pj_ota_transfer_result_t;

typedef struct {
    pj_ota_transfer_state_t state;
    char upload_id[PJ_OTA_UPLOAD_ID_LEN + 1U];
    uint64_t expected_bytes;
    uint64_t received_bytes;
    int mutations_reserved;
} pj_ota_session_t;

void pj_ota_session_init(pj_ota_session_t *session);
pj_ota_transfer_result_t pj_ota_session_prepare(pj_ota_session_t *session,
                                                const char *upload_id,
                                                uint64_t expected_bytes);
pj_ota_transfer_result_t pj_ota_session_reserve(pj_ota_session_t *session,
                                                const char *upload_id,
                                                int mutations_reserved);
pj_ota_transfer_result_t pj_ota_session_begin(pj_ota_session_t *session,
                                              const char *upload_id);
pj_ota_transfer_result_t pj_ota_session_write(pj_ota_session_t *session,
                                              const char *upload_id,
                                              uint64_t offset,
                                              size_t bytes);
pj_ota_transfer_result_t pj_ota_session_finish(pj_ota_session_t *session,
                                               const char *upload_id);
void pj_ota_session_abort(pj_ota_session_t *session);

typedef enum {
    PJ_OTA_BOOT_IDLE = 0,
    PJ_OTA_BOOT_TESTING,
    PJ_OTA_BOOT_CONFIRMED,
    PJ_OTA_BOOT_ROLLBACK_REQUIRED,
    PJ_OTA_BOOT_ROLLED_BACK,
    PJ_OTA_BOOT_FAILED,
} pj_ota_boot_state_t;

typedef struct {
    int update_recorded;
    int running_matches_target;
    int running_pending_verify;
    int health_checked;
    int health_ok;
    int rollback_possible;
} pj_ota_boot_inputs_t;

pj_ota_boot_state_t pj_ota_boot_evaluate(const pj_ota_boot_inputs_t *inputs);

#ifdef __cplusplus
}
#endif
