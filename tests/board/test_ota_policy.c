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
    running.version = "e4805d6";
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) == PJ_OTA_CHECK_ACCEPTED);
    strcpy(candidate.version, "7e6ce1e-dirty");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) ==
           PJ_OTA_CHECK_VERSION_FORMAT);
    running = identity();
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) ==
           PJ_OTA_CHECK_VERSION_FORMAT);
    strcpy(candidate.version, "v2.0.0");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) ==
           PJ_OTA_CHECK_VERSION_FORMAT);
    strcpy(candidate.version, "02.0.0");
    assert(pj_ota_preflight_check(&candidate, &running, 1, 1, 1, 0) ==
           PJ_OTA_CHECK_VERSION_FORMAT);
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
    const char *replacement = "11111111111111111111111111111111";
    assert(pj_ota_session_prepare(&session, replacement, 12U) == PJ_OTA_TRANSFER_OK);
    assert(session.expected_bytes == 12U);
    assert(pj_ota_session_begin(&session, id) == PJ_OTA_TRANSFER_ID_MISMATCH);
    assert(pj_ota_session_prepare(&session, id, 10U) == PJ_OTA_TRANSFER_OK);
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

static void test_mutation_reservation_is_request_owned(void)
{
    pj_ota_mutation_reservation_t reservation = {0};
    pj_ota_mutation_lease_t owner = {0};
    pj_ota_mutation_lease_t concurrent = {0};

    assert(pj_ota_mutation_reservation_claim(&reservation, &owner));
    assert(pj_ota_mutation_reservation_held(&reservation));
    assert(!pj_ota_mutation_reservation_claim(&reservation, &concurrent));
    assert(!pj_ota_mutation_reservation_release(&reservation, &concurrent));
    assert(pj_ota_mutation_reservation_held(&reservation));
    assert(pj_ota_mutation_reservation_release(&reservation, &owner));
    assert(!pj_ota_mutation_reservation_held(&reservation));
    assert(!pj_ota_mutation_reservation_release(&reservation, &owner));
}

static void test_activation_health_reset_and_factory_migration(void)
{
    pj_ota_boot_inputs_t boot = {0};
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_IDLE); /* factory migration */

    boot.update_recorded = 1;
    boot.record_state = PJ_OTA_RECORD_PENDING_REBOOT;
    boot.running_version_matches_target = 1;
    boot.running_partition_matches_target = 1;
    boot.running_pending_verify = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_TESTING);
    boot.record_state = PJ_OTA_RECORD_TESTING;
    boot.health_checked = 1;
    boot.health_ok = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_CONFIRMED);
    boot.health_ok = 0;
    boot.rollback_possible = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_ROLLBACK_REQUIRED);
    boot.rollback_possible = 0;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);

    /* A reset before confirmation boots the previous image and preserves the target record. */
    boot.running_partition_matches_target = 0;
    boot.running_pending_verify = 0;
    boot.health_checked = 0;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_ROLLED_BACK);
}

static void test_missing_record_pending_verify_fails_closed(void)
{
    assert(!pj_ota_unrecorded_boot_requires_recovery(
        PJ_OTA_BOOT_PARTITION_FACTORY, 0, 0));
    assert(!pj_ota_unrecorded_boot_requires_recovery(
        PJ_OTA_BOOT_PARTITION_OTA, 1, 1));
    assert(pj_ota_unrecorded_boot_requires_recovery(
        PJ_OTA_BOOT_PARTITION_OTA, 1, 0));
    assert(pj_ota_unrecorded_boot_requires_recovery(
        PJ_OTA_BOOT_PARTITION_OTA, 0, 0));
    assert(pj_ota_unrecorded_boot_requires_recovery(
        PJ_OTA_BOOT_PARTITION_OTA, 0, 1));
    assert(pj_ota_unrecorded_boot_requires_recovery(
        PJ_OTA_BOOT_PARTITION_UNKNOWN, 0, 0));
    assert(pj_ota_unrecorded_boot_requires_recovery(
        PJ_OTA_BOOT_PARTITION_UNKNOWN, 1, 1));

    pj_ota_boot_inputs_t boot = {0};
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_IDLE);
    assert(!pj_ota_boot_recovery_active(0, 0));

    boot.running_pending_verify = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);
    assert(pj_ota_boot_recovery_active(1, 0));
    assert(pj_ota_boot_recovery_active(1, 1));

    boot.health_checked = 1;
    boot.health_ok = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);
    boot.rollback_possible = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_ROLLBACK_REQUIRED);

    boot.running_pending_verify = 0;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_IDLE);
    assert(pj_ota_boot_recovery_active(0, 1));
}

static void test_persisted_state_boot_matrix(void)
{
    assert(pj_ota_record_state_parse("pending_reboot") ==
           PJ_OTA_RECORD_PENDING_REBOOT);
    assert(pj_ota_record_state_parse("testing") == PJ_OTA_RECORD_TESTING);
    assert(pj_ota_record_state_parse("confirmed") == PJ_OTA_RECORD_CONFIRMED);
    assert(pj_ota_record_state_parse("failed_health") ==
           PJ_OTA_RECORD_FAILED_HEALTH);
    assert(pj_ota_record_state_parse("rollback_requested") ==
           PJ_OTA_RECORD_ROLLBACK_REQUESTED);
    assert(pj_ota_record_state_parse("rolled_back") ==
           PJ_OTA_RECORD_ROLLED_BACK);
    assert(pj_ota_record_state_parse("failed") == PJ_OTA_RECORD_FAILED);
    assert(pj_ota_record_state_parse("unknown") == PJ_OTA_RECORD_INVALID);
    assert(pj_ota_record_state_parse(NULL) == PJ_OTA_RECORD_INVALID);

    pj_ota_boot_inputs_t boot = {
        .update_recorded = 1,
        .running_version_matches_target = 1,
        .running_partition_matches_target = 1,
        .running_pending_verify = 1,
    };
    const pj_ota_record_state_t rejected[] = {
        PJ_OTA_RECORD_INVALID,
        PJ_OTA_RECORD_CONFIRMED,
        PJ_OTA_RECORD_FAILED_HEALTH,
        PJ_OTA_RECORD_ROLLBACK_REQUESTED,
        PJ_OTA_RECORD_ROLLED_BACK,
        PJ_OTA_RECORD_FAILED,
    };
    for (size_t index = 0U;
         index < sizeof(rejected) / sizeof(rejected[0]); index++) {
        boot.record_state = rejected[index];
        assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);
        boot.health_checked = 1;
        boot.health_ok = 1;
        boot.rollback_possible = 1;
        assert(pj_ota_boot_evaluate(&boot) ==
               PJ_OTA_BOOT_ROLLBACK_REQUIRED);
        boot.health_checked = 0;
        boot.health_ok = 0;
        boot.rollback_possible = 0;
    }

    boot.running_pending_verify = 0;
    boot.record_state = PJ_OTA_RECORD_CONFIRMED;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_CONFIRMED);
    boot.record_state = PJ_OTA_RECORD_PENDING_REBOOT;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_CONFIRMED);
}

static void test_failed_health_is_irrevocable_across_failures(void)
{
    pj_ota_boot_inputs_t boot = {
        .update_recorded = 1,
        .record_state = PJ_OTA_RECORD_TESTING,
        .running_version_matches_target = 1,
        .running_partition_matches_target = 1,
        .running_pending_verify = 1,
    };

    /* NVS failed-health write fails: durable testing means reboot cannot confirm. */
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);
    boot.record_state = PJ_OTA_RECORD_FAILED_HEALTH;
    boot.health_checked = 1;
    boot.health_ok = 1;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);

    /* A later healthy observation cannot override failed health. */
    boot.rollback_possible = 1;
    assert(pj_ota_boot_evaluate(&boot) ==
           PJ_OTA_BOOT_ROLLBACK_REQUIRED);

    /* A failed rollback call leaves the same durable marker for the next boot. */
    boot.health_checked = 0;
    boot.health_ok = 0;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);
    boot.health_checked = 1;
    assert(pj_ota_boot_evaluate(&boot) ==
           PJ_OTA_BOOT_ROLLBACK_REQUIRED);
}

static void test_torn_confirmation_reconciles_only_nonfailed_records(void)
{
    pj_ota_boot_inputs_t boot = {
        .update_recorded = 1,
        .record_state = PJ_OTA_RECORD_TESTING,
        .running_version_matches_target = 1,
        .running_partition_matches_target = 1,
        .running_pending_verify = 0,
    };

    /* IDF VALID proves mark-valid completed after health; the NVS write tore. */
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_CONFIRMED);
    boot.record_state = PJ_OTA_RECORD_CONFIRMED;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_CONFIRMED);
    boot.record_state = PJ_OTA_RECORD_PENDING_REBOOT;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_CONFIRMED);

    boot.record_state = PJ_OTA_RECORD_FAILED_HEALTH;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);
    boot.record_state = PJ_OTA_RECORD_ROLLBACK_REQUESTED;
    assert(pj_ota_boot_evaluate(&boot) == PJ_OTA_BOOT_FAILED);
}

static void test_failure_retry_plan_bounds_nvs_writes(void)
{
    pj_ota_failure_retry_plan_t plan =
        pj_ota_failure_retry_plan(0, 1, 0, 0U);
    assert(plan.active && plan.write_terminal_marker &&
           !plan.attempt_rollback);

    plan = pj_ota_failure_retry_plan(1, 1, 0, 0U);
    assert(!plan.active && !plan.write_terminal_marker &&
           !plan.attempt_rollback);

    plan = pj_ota_failure_retry_plan(1, 1, 1, 0U);
    assert(plan.active && !plan.write_terminal_marker &&
           plan.attempt_rollback);

    unsigned writes = 0U;
    int marker_persisted = 0;
    for (unsigned attempt = 0U; attempt < PJ_OTA_FAILURE_RETRY_LIMIT;
         attempt++) {
        plan = pj_ota_failure_retry_plan(marker_persisted, 1, 0, attempt);
        if (!plan.active) {
            break;
        }
        if (plan.write_terminal_marker) {
            writes++;
            marker_persisted = attempt == 2U;
        }
    }
    assert(writes == 3U);
    plan = pj_ota_failure_retry_plan(marker_persisted, 1, 0, 3U);
    assert(!plan.active); /* No NVS writes after the first success. */

    plan = pj_ota_failure_retry_plan(
        0, 1, 1, PJ_OTA_FAILURE_RETRY_LIMIT);
    assert(!plan.active); /* Persistent faults are bounded. */

    writes = 0U;
    for (unsigned attempt = 0U; attempt < PJ_OTA_FAILURE_RETRY_LIMIT;
         attempt++) {
        plan = pj_ota_failure_retry_plan(0, 1, 0, attempt);
        assert(plan.active && plan.write_terminal_marker);
        writes++;
    }
    assert(writes == PJ_OTA_FAILURE_RETRY_LIMIT);

    for (unsigned attempt = 0U; attempt < PJ_OTA_FAILURE_RETRY_LIMIT;
         attempt++) {
        plan = pj_ota_failure_retry_plan(0, 0, 1, attempt);
        assert(plan.active && !plan.write_terminal_marker &&
               plan.attempt_rollback);
    }
    plan = pj_ota_failure_retry_plan(0, 0, 0, 0U);
    assert(!plan.active); /* Missing target identity cannot produce a marker. */
}

int main(void)
{
    test_preflight_rejection_matrix();
    test_canonical_manifest_and_image_validation();
    test_transfer_interruption_concurrency_and_replay();
    test_mutation_reservation_is_request_owned();
    test_activation_health_reset_and_factory_migration();
    test_missing_record_pending_verify_fails_closed();
    test_persisted_state_boot_matrix();
    test_failed_health_is_irrevocable_across_failures();
    test_torn_confirmation_reconciles_only_nonfailed_records();
    test_failure_retry_plan_bounds_nvs_writes();
    puts("OTA policy tests passed");
    return 0;
}
