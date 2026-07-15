#include "pj_ota_policy.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int sha256_valid(const char *value)
{
    if (value == NULL || strlen(value) != PJ_OTA_SHA256_HEX_LEN) {
        return 0;
    }
    for (size_t i = 0; i < PJ_OTA_SHA256_HEX_LEN; i++) {
        if (!isxdigit((unsigned char)value[i]) || isupper((unsigned char)value[i])) {
            return 0;
        }
    }
    return 1;
}

static int signed_text_valid(const char *value, size_t capacity)
{
    if (value == NULL) {
        return 0;
    }
    size_t length = strnlen(value, capacity);
    if (length == 0U || length >= capacity) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char)value[i];
        if (!(isalnum(character) || character == '.' || character == '_' ||
              character == '-' || character == '+')) {
            return 0;
        }
    }
    return 1;
}

static int parse_semver(const char *value, uint32_t parts[3])
{
    if (value == NULL) {
        return 0;
    }
    const char *cursor = value;
    for (size_t part = 0; part < 3U; part++) {
        if (!isdigit((unsigned char)*cursor)) {
            return 0;
        }
        if (*cursor == '0' && isdigit((unsigned char)cursor[1])) {
            return 0;
        }
        uint64_t number = 0U;
        do {
            number = number * 10U + (uint64_t)(*cursor - '0');
            if (number > UINT32_MAX) {
                return 0;
            }
            cursor++;
        } while (isdigit((unsigned char)*cursor));
        parts[part] = (uint32_t)number;
        if (part < 2U) {
            if (*cursor != '.') {
                return 0;
            }
            cursor++;
        }
    }
    return *cursor == '\0';
}

static int semver_compare(const char *left, const char *right, int *comparable)
{
    uint32_t left_parts[3];
    uint32_t right_parts[3];
    *comparable = parse_semver(left, left_parts) && parse_semver(right, right_parts);
    if (!*comparable) {
        return 0;
    }
    for (size_t i = 0; i < 3U; i++) {
        if (left_parts[i] < right_parts[i]) {
            return -1;
        }
        if (left_parts[i] > right_parts[i]) {
            return 1;
        }
    }
    return 0;
}

const char *pj_ota_check_code(pj_ota_check_t result)
{
    static const char *const codes[] = {
        "accepted", "ota_busy", "verification_key_unconfigured",
        "signed_app_verification_unconfigured", "signature_invalid",
        "manifest_invalid", "image_size_invalid",
        "project_mismatch", "board_mismatch", "target_mismatch",
        "version_replay", "version_downgrade", "version_format_rejected",
        "secure_version_rejected",
        "image_invalid", "image_digest_mismatch", "image_descriptor_mismatch",
        "active_partition_rejected",
    };
    size_t index = (size_t)result;
    return index < sizeof(codes) / sizeof(codes[0]) ? codes[index] : "unknown";
}

int pj_ota_manifest_valid(const pj_ota_manifest_t *manifest)
{
    return manifest != NULL && manifest->size > 0U &&
           sha256_valid(manifest->sha256) &&
           signed_text_valid(manifest->project, sizeof(manifest->project)) &&
           signed_text_valid(manifest->board, sizeof(manifest->board)) &&
           signed_text_valid(manifest->target, sizeof(manifest->target)) &&
           signed_text_valid(manifest->version, sizeof(manifest->version));
}

int pj_ota_manifest_canonicalize(const pj_ota_manifest_t *manifest,
                                 char *output,
                                 size_t output_size,
                                 size_t *written)
{
    if (!pj_ota_manifest_valid(manifest) || output == NULL || output_size == 0U) {
        return 0;
    }
    int length = snprintf(output, output_size,
                          "PJOTA1\nsha256=%s\nsize=%llu\nproject=%s\nboard=%s\n"
                          "target=%s\nversion=%s\nsecure_version=%lu\n",
                          manifest->sha256,
                          (unsigned long long)manifest->size,
                          manifest->project,
                          manifest->board,
                          manifest->target,
                          manifest->version,
                          (unsigned long)manifest->secure_version);
    if (length < 0 || (size_t)length >= output_size) {
        return 0;
    }
    if (written != NULL) {
        *written = (size_t)length;
    }
    return 1;
}

pj_ota_check_t pj_ota_preflight_check(const pj_ota_manifest_t *manifest,
                                      const pj_ota_identity_t *identity,
                                      int verification_key_configured,
                                      int signed_app_verification_configured,
                                      int signature_verified,
                                      int session_active)
{
    if (session_active) {
        return PJ_OTA_CHECK_BUSY;
    }
    if (!verification_key_configured) {
        return PJ_OTA_CHECK_KEY_UNCONFIGURED;
    }
    if (!signed_app_verification_configured) {
        return PJ_OTA_CHECK_SIGNED_APP_UNCONFIGURED;
    }
    if (!pj_ota_manifest_valid(manifest) || identity == NULL ||
        identity->project == NULL || identity->board == NULL ||
        identity->target == NULL || identity->version == NULL) {
        return PJ_OTA_CHECK_MANIFEST_INVALID;
    }
    if (!signature_verified) {
        return PJ_OTA_CHECK_SIGNATURE_INVALID;
    }
    if (manifest->size > identity->partition_capacity) {
        return PJ_OTA_CHECK_SIZE_INVALID;
    }
    if (strcmp(manifest->project, identity->project) != 0) {
        return PJ_OTA_CHECK_PROJECT_MISMATCH;
    }
    if (strcmp(manifest->board, identity->board) != 0) {
        return PJ_OTA_CHECK_BOARD_MISMATCH;
    }
    if (strcmp(manifest->target, identity->target) != 0) {
        return PJ_OTA_CHECK_TARGET_MISMATCH;
    }
    if (strcmp(manifest->version, identity->version) == 0) {
        return PJ_OTA_CHECK_VERSION_REPLAY;
    }
    uint32_t candidate_parts[3];
    uint32_t running_parts[3];
    int candidate_semver = parse_semver(manifest->version, candidate_parts);
    int running_semver = parse_semver(identity->version, running_parts);
    if (!candidate_semver) {
        return PJ_OTA_CHECK_VERSION_FORMAT;
    }
    if (running_semver) {
        int comparable = 0;
        if (semver_compare(manifest->version, identity->version,
                           &comparable) < 0 && comparable) {
            return PJ_OTA_CHECK_VERSION_DOWNGRADE;
        }
    } else {
        /* The factory/dev build gets one migration into the semver sequence. */
    }
    if (manifest->secure_version < identity->secure_version) {
        return PJ_OTA_CHECK_SECURE_VERSION;
    }
    return PJ_OTA_CHECK_ACCEPTED;
}

pj_ota_check_t pj_ota_image_check(const pj_ota_manifest_t *manifest,
                                  const char *image_project,
                                  const char *image_version,
                                  uint32_t image_secure_version,
                                  int digest_matches,
                                  int image_verified,
                                  int target_verified,
                                  int partition_is_inactive)
{
    if (!partition_is_inactive) {
        return PJ_OTA_CHECK_PARTITION_ACTIVE;
    }
    if (!image_verified) {
        return PJ_OTA_CHECK_IMAGE_INVALID;
    }
    if (!target_verified) {
        return PJ_OTA_CHECK_TARGET_MISMATCH;
    }
    if (!digest_matches) {
        return PJ_OTA_CHECK_IMAGE_DIGEST;
    }
    if (!pj_ota_manifest_valid(manifest) || image_project == NULL ||
        image_version == NULL || strcmp(image_project, manifest->project) != 0 ||
        strcmp(image_version, manifest->version) != 0 ||
        image_secure_version != manifest->secure_version) {
        return PJ_OTA_CHECK_IMAGE_DESCRIPTOR;
    }
    return PJ_OTA_CHECK_ACCEPTED;
}

void pj_ota_session_init(pj_ota_session_t *session)
{
    if (session != NULL) {
        memset(session, 0, sizeof(*session));
    }
}

static int upload_id_valid(const char *upload_id)
{
    if (upload_id == NULL) {
        return 0;
    }
    size_t length = strlen(upload_id);
    if (length != PJ_OTA_UPLOAD_ID_LEN) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)upload_id[i]) || isupper((unsigned char)upload_id[i])) {
            return 0;
        }
    }
    return 1;
}

pj_ota_transfer_result_t pj_ota_session_prepare(pj_ota_session_t *session,
                                                const char *upload_id,
                                                uint64_t expected_bytes)
{
    if (session == NULL || !upload_id_valid(upload_id) || expected_bytes == 0U) {
        return PJ_OTA_TRANSFER_INVALID_STATE;
    }
    if (session->state == PJ_OTA_TRANSFER_WRITING) {
        return PJ_OTA_TRANSFER_BUSY;
    }
    if (session->state == PJ_OTA_TRANSFER_PENDING_REBOOT) {
        return strcmp(session->upload_id, upload_id) == 0 ?
            PJ_OTA_TRANSFER_REPLAY : PJ_OTA_TRANSFER_BUSY;
    }
    if (session->state == PJ_OTA_TRANSFER_READY &&
        strcmp(session->upload_id, upload_id) == 0) {
        return PJ_OTA_TRANSFER_REPLAY;
    }
    memset(session, 0, sizeof(*session));
    session->state = PJ_OTA_TRANSFER_READY;
    memcpy(session->upload_id, upload_id, PJ_OTA_UPLOAD_ID_LEN + 1U);
    session->expected_bytes = expected_bytes;
    return PJ_OTA_TRANSFER_OK;
}

pj_ota_transfer_result_t pj_ota_session_begin(pj_ota_session_t *session,
                                              const char *upload_id)
{
    if (session == NULL || session->state != PJ_OTA_TRANSFER_READY) {
        if (session != NULL && session->state == PJ_OTA_TRANSFER_WRITING) {
            return PJ_OTA_TRANSFER_BUSY;
        }
        if (session != NULL && session->state == PJ_OTA_TRANSFER_PENDING_REBOOT) {
            return PJ_OTA_TRANSFER_REPLAY;
        }
        return PJ_OTA_TRANSFER_INVALID_STATE;
    }
    if (upload_id == NULL || strcmp(session->upload_id, upload_id) != 0) {
        return PJ_OTA_TRANSFER_ID_MISMATCH;
    }
    if (!session->mutations_reserved) {
        return PJ_OTA_TRANSFER_BUSY;
    }
    session->state = PJ_OTA_TRANSFER_WRITING;
    session->received_bytes = 0U;
    return PJ_OTA_TRANSFER_OK;
}

pj_ota_transfer_result_t pj_ota_session_reserve(pj_ota_session_t *session,
                                                const char *upload_id,
                                                int mutations_reserved)
{
    if (session == NULL || session->state != PJ_OTA_TRANSFER_READY) {
        if (session != NULL && session->state == PJ_OTA_TRANSFER_WRITING) {
            return PJ_OTA_TRANSFER_BUSY;
        }
        if (session != NULL && session->state == PJ_OTA_TRANSFER_PENDING_REBOOT) {
            return PJ_OTA_TRANSFER_REPLAY;
        }
        return PJ_OTA_TRANSFER_INVALID_STATE;
    }
    if (upload_id == NULL || strcmp(session->upload_id, upload_id) != 0) {
        return PJ_OTA_TRANSFER_ID_MISMATCH;
    }
    if (!mutations_reserved) {
        return PJ_OTA_TRANSFER_BUSY;
    }
    session->mutations_reserved = 1;
    return PJ_OTA_TRANSFER_OK;
}

pj_ota_transfer_result_t pj_ota_session_write(pj_ota_session_t *session,
                                              const char *upload_id,
                                              uint64_t offset,
                                              size_t bytes)
{
    if (session == NULL || session->state != PJ_OTA_TRANSFER_WRITING) {
        return PJ_OTA_TRANSFER_INVALID_STATE;
    }
    if (upload_id == NULL || strcmp(session->upload_id, upload_id) != 0) {
        return PJ_OTA_TRANSFER_ID_MISMATCH;
    }
    if (offset != session->received_bytes) {
        return PJ_OTA_TRANSFER_OFFSET_MISMATCH;
    }
    if ((uint64_t)bytes > session->expected_bytes - session->received_bytes) {
        return PJ_OTA_TRANSFER_OVERFLOW;
    }
    session->received_bytes += (uint64_t)bytes;
    return PJ_OTA_TRANSFER_OK;
}

pj_ota_transfer_result_t pj_ota_session_finish(pj_ota_session_t *session,
                                               const char *upload_id)
{
    if (session == NULL || session->state != PJ_OTA_TRANSFER_WRITING) {
        return PJ_OTA_TRANSFER_INVALID_STATE;
    }
    if (upload_id == NULL || strcmp(session->upload_id, upload_id) != 0) {
        return PJ_OTA_TRANSFER_ID_MISMATCH;
    }
    if (session->received_bytes != session->expected_bytes) {
        return PJ_OTA_TRANSFER_INCOMPLETE;
    }
    session->state = PJ_OTA_TRANSFER_PENDING_REBOOT;
    return PJ_OTA_TRANSFER_OK;
}

void pj_ota_session_abort(pj_ota_session_t *session)
{
    if (session != NULL && session->state != PJ_OTA_TRANSFER_PENDING_REBOOT) {
        session->state = PJ_OTA_TRANSFER_FAILED;
        session->received_bytes = 0U;
        session->mutations_reserved = 0;
    }
}

int pj_ota_mutation_reservation_held(
    const pj_ota_mutation_reservation_t *reservation)
{
    return reservation != NULL && reservation->held;
}

int pj_ota_mutation_reservation_claim(
    pj_ota_mutation_reservation_t *reservation,
    pj_ota_mutation_lease_t *lease)
{
    if (reservation == NULL || lease == NULL || reservation->held) {
        return 0;
    }
    uint32_t generation = reservation->generation + 1U;
    if (generation == 0U) {
        generation = 1U;
    }
    reservation->generation = generation;
    reservation->held = 1;
    lease->generation = generation;
    return 1;
}

int pj_ota_mutation_reservation_release(
    pj_ota_mutation_reservation_t *reservation,
    pj_ota_mutation_lease_t *lease)
{
    if (reservation == NULL || lease == NULL || !reservation->held ||
        lease->generation == 0U ||
        lease->generation != reservation->generation) {
        return 0;
    }
    reservation->held = 0;
    lease->generation = 0U;
    return 1;
}

pj_ota_record_state_t pj_ota_record_state_parse(const char *state)
{
    static const char *const names[] = {
        NULL,
        "pending_reboot",
        "testing",
        "confirmed",
        "failed_health",
        "rollback_requested",
        "rolled_back",
        "failed",
    };
    if (state == NULL) {
        return PJ_OTA_RECORD_INVALID;
    }
    for (size_t index = 1U; index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(state, names[index]) == 0) {
            return (pj_ota_record_state_t)index;
        }
    }
    return PJ_OTA_RECORD_INVALID;
}

pj_ota_boot_state_t pj_ota_boot_evaluate(const pj_ota_boot_inputs_t *inputs)
{
    if (inputs == NULL || !inputs->update_recorded) {
        return PJ_OTA_BOOT_IDLE;
    }
    if (!inputs->running_version_matches_target ||
        !inputs->running_partition_matches_target) {
        return PJ_OTA_BOOT_ROLLED_BACK;
    }
    if (!inputs->running_pending_verify &&
        (inputs->record_state == PJ_OTA_RECORD_PENDING_REBOOT ||
         inputs->record_state == PJ_OTA_RECORD_TESTING ||
         inputs->record_state == PJ_OTA_RECORD_CONFIRMED)) {
        return PJ_OTA_BOOT_CONFIRMED;
    }
    if (inputs->record_state == PJ_OTA_RECORD_PENDING_REBOOT) {
        return !inputs->running_pending_verify || inputs->health_checked ?
            PJ_OTA_BOOT_FAILED : PJ_OTA_BOOT_TESTING;
    }
    if (inputs->record_state != PJ_OTA_RECORD_TESTING) {
        return inputs->running_pending_verify && inputs->health_checked &&
            inputs->rollback_possible ? PJ_OTA_BOOT_ROLLBACK_REQUIRED :
            PJ_OTA_BOOT_FAILED;
    }
    if (!inputs->running_pending_verify) {
        return PJ_OTA_BOOT_CONFIRMED;
    }
    if (!inputs->health_checked) {
        return PJ_OTA_BOOT_FAILED;
    }
    if (inputs->health_ok) {
        return PJ_OTA_BOOT_CONFIRMED;
    }
    return inputs->rollback_possible ? PJ_OTA_BOOT_ROLLBACK_REQUIRED : PJ_OTA_BOOT_FAILED;
}

pj_ota_failure_retry_plan_t pj_ota_failure_retry_plan(
    int terminal_marker_persisted,
    int rollback_possible,
    unsigned attempts_completed)
{
    pj_ota_failure_retry_plan_t plan = {0};
    if (attempts_completed >= PJ_OTA_FAILURE_RETRY_LIMIT ||
        (terminal_marker_persisted && !rollback_possible)) {
        return plan;
    }
    plan.active = 1;
    plan.write_terminal_marker = !terminal_marker_persisted;
    plan.attempt_rollback = rollback_possible;
    return plan;
}
