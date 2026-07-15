#include "pj_ota_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static pj_ota_manifest_t manifest(void)
{
    pj_ota_manifest_t value = {
        .size = 1024U,
        .sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .project = "pocket_journal",
        .board = "waveshare-esp32-s3-touch-epaper-1.54-v2",
        .target = "esp32s3",
        .version = "2.0.0",
        .secure_version = 3U,
    };
    return value;
}

static pj_ota_identity_t identity(void)
{
    pj_ota_identity_t value = {
        .project = "pocket_journal",
        .board = "waveshare-esp32-s3-touch-epaper-1.54-v2",
        .target = "esp32s3",
        .version = "1.0.0",
        .secure_version = 2U,
        .partition_capacity = 2U * 1024U * 1024U,
    };
    return value;
}

static void test_preflight_rejection_matrix(void)
{
    pj_ota_manifest_t candidate = manifest();
    pj_ota_identity_t running = identity();
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_ACCEPTED);
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 1) == PJ_OTA_CHECK_BUSY);
    assert(pj_ota_preflight_check(&candidate, &running, 0, 1, 0, 0) == PJ_OTA_CHECK_KEY_UNCONFIGURED);
    assert(pj_ota_preflight_check(&candidate, &running, 1, 0, 1, 0) ==
           PJ_OTA_CHECK_SIGNED_APP_UNCONFIGURED);
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 0, 0) == PJ_OTA_CHECK_SIGNATURE_INVALID);

    strcpy(candidate.project, "other");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_PROJECT_MISMATCH);
    candidate = manifest();
    strcpy(candidate.board, "other-board");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_BOARD_MISMATCH);
    candidate = manifest();
    strcpy(candidate.target, "esp32");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_TARGET_MISMATCH);
    candidate = manifest();
    strcpy(candidate.version, "1.0.0");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_VERSION_REPLAY);
    strcpy(candidate.version, "0.9.0");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_VERSION_DOWNGRADE);
    candidate = manifest();
    strcpy(candidate.version, "7e6ce1e-dirty");
    running.version = "e4805d6";
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_ACCEPTED);
    running = identity();
    candidate = manifest();
    candidate.secure_version = 1U;
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_SECURE_VERSION);
    candidate = manifest();
    candidate.size = running.partition_capacity + 1U;
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_SIZE_INVALID);
}

static void test_canonical_manifest_and_image_validation(void)
{
    pj_ota_manifest_t candidate = manifest();
    char canonical[PJ_OTA_CANONICAL_MAX];
    size_t length = 0U;
    assert(pj_ota_manifest_canonicalize(&candidate, canonical, sizeof(canonical), &length));
    assert(length == strlen(canonical));
    assert(strstr(canonical, "PJOTA1\nsha256=") == canonical);
    assert(strstr(canonical, "\nboard=waveshare-esp32-s3-touch-epaper-1.54-v2\n") != NULL);
    assert(pj_ota_image_check(&candidate, candidate.project, candidate.version,
                              candidate.secure_version, 1, 1, 1, 1) == PJ_OTA_CHECK_ACCEPTED);
    assert(pj_ota_image_check(&candidate, candidate.project, candidate.version,
                              candidate.secure_version, 0, 1, 1, 1) == PJ_OTA_CHECK_IMAGE_DIGEST);
    assert(pj_ota_image_check(&candidate, candidate.project, candidate.version,
                              candidate.secure_version, 1, 0, 1, 1) == PJ_OTA_CHECK_IMAGE_INVALID);
    assert(pj_ota_image_check(&candidate, candidate.project, candidate.version,
                              candidate.secure_version, 1, 1, 0, 1) == PJ_OTA_CHECK_TARGET_MISMATCH);
    assert(pj_ota_image_check(&candidate, "other", candidate.version,
                              candidate.secure_version, 1, 1, 1, 1) == PJ_OTA_CHECK_IMAGE_DESCRIPTOR);
    assert(pj_ota_image_check(&candidate, candidate.project, candidate.version,
                              candidate.secure_version, 1, 1, 1, 0) == PJ_OTA_CHECK_PARTITION_ACTIVE);
}

static void test_transfer_interruption_concurrency_and_replay(void)
{
    const char *id = "0123456789abcdef0123456789abcdef";
    pj_ota_session_t session;
    pj_ota_session_init(&session);
    assert(pj_ota_session_prepare(&session, id, 10U) == PJ_OTA_TRANSFER_OK);
    assert(!session.mutations_reserved); /* Preflight is advisory and nonblocking. */
    assert(pj_ota_session_prepare(&session, id, 10U) == PJ_OTA_TRANSFER_REPLAY);
    assert(pj_ota_session_begin(&session, "ffffffffffffffffffffffffffffffff") ==
           PJ_OTA_TRANSFER_ID_MISMATCH);
    assert(pj_ota_session_reserve(&session, id, 0) == PJ_OTA_TRANSFER_BUSY);
    assert(pj_ota_session_begin(&session, id) == PJ_OTA_TRANSFER_BUSY);
    assert(pj_ota_session_reserve(&session, id, 1) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_begin(&session, id) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_begin(&session, id) == PJ_OTA_TRANSFER_BUSY);
    assert(pj_ota_session_write(&session, id, 0U, 6U) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_write(&session, id, 0U, 1U) == PJ_OTA_TRANSFER_OFFSET_MISMATCH);
    assert(pj_ota_session_write(&session, id, 6U, 5U) == PJ_OTA_TRANSFER_OVERFLOW);
    assert(pj_ota_session_finish(&session, id) == PJ_OTA_TRANSFER_INCOMPLETE);
    pj_ota_session_abort(&session);
    assert(session.state == PJ_OTA_TRANSFER_FAILED && session.received_bytes == 0U);

    assert(pj_ota_session_prepare(&session, id, 10U) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_reserve(&session, id, 1) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_begin(&session, id) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_write(&session, id, 0U, 10U) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_finish(&session, id) == PJ_OTA_TRANSFER_OK);
    assert(pj_ota_session_reserve(&session, id, 1) == PJ_OTA_TRANSFER_REPLAY);
    assert(pj_ota_session_prepare(&session, id, 10U) == PJ_OTA_TRANSFER_REPLAY);
    pj_ota_session_abort(&session);
    assert(session.state == PJ_OTA_TRANSFER_PENDING_REBOOT);
}

static void test_activation_health_reset_and_factory_migration(void)
{
    pj_ota_boot_inputs_t boot = {0};
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_IDLE); /* factory migration */

    boot.update_recorded = 1;
    boot.running_matches_target = 1;
    boot.running_pending_verify = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_TESTING);
    boot.health_checked = 1;
    boot.health_ok = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_CONFIRMED);
    boot.health_ok = 0;
    boot.rollback_possible = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_ROLLBACK_REQUIRED);
    boot.rollback_possible = 0;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);

    /* A reset before confirmation boots the previous image and preserves the target record. */
    boot.running_matches_target = 0;
    boot.running_pending_verify = 0;
    boot.health_checked = 0;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_ROLLED_BACK);
}

int main(void)
{
    test_preflight_rejection_matrix();
    test_canonical_manifest_and_image_validation();
    test_transfer_interruption_concurrency_and_replay();
    test_activation_health_reset_and_factory_migration();
    puts("OTA policy tests passed");
    return 0;
}
