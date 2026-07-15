from __future__ import annotations

import copy
import unittest

from pocket_journal_partner.companion_auth import (
    CompanionAuthenticationError,
    CompanionProtocolError,
    canonical_request,
    normalize_error,
    pairing_epoch,
    provisioned_token,
    request_envelope,
    response_envelope,
    scoped_data_token,
    verify_request_envelope,
    verify_response_envelope,
)


TOKEN = "paired-token-0123456789"
DEVICE = "pj-test"
DEVICE_IP = "192.0.2.10"
OPERATION = "pj-test-00000007"
NONCE = "00112233445566778899aabbccddeeff"


class CompanionAuthenticationTests(unittest.TestCase):
    def test_request_is_canonical_and_bound_to_direct_peer(self) -> None:
        canonical = canonical_request(
            "start", DEVICE, DEVICE_IP, OPERATION, 7, 123456789, NONCE
        )
        self.assertEqual(
            canonical,
            b"PJ-COMPANION-SYNC-REQUEST-V1\n"
            b"version=1\n"
            b"action=5:start\n"
            b"device_id=7:pj-test\n"
            b"device_ip=10:192.0.2.10\n"
            b"operation_id=16:pj-test-00000007\n"
            b"generation=7\n"
            b"requested_ms=123456789\n"
            b"nonce=32:00112233445566778899aabbccddeeff\n",
        )
        envelope = request_envelope(
            TOKEN, "start", DEVICE, DEVICE_IP, OPERATION, 7, 123456789,
            NONCE,
        )
        self.assertEqual(
            envelope["mac"],
            "312b32119bcec58bcf833b11fa22683ef79944fca2fed53d60136546b48c7641",
        )
        verified = verify_request_envelope(
            envelope,
            TOKEN,
            expected_action="start",
            expected_device_id=DEVICE,
            expected_peer_ip=DEVICE_IP,
        )
        self.assertEqual(verified["device_ip"], DEVICE_IP)

    def test_forwarded_request_and_noncanonical_ipv4_fail_closed(self) -> None:
        envelope = request_envelope(
            TOKEN, "start", DEVICE, DEVICE_IP, OPERATION, 7, 123456789,
            NONCE,
        )
        with self.assertRaises(CompanionAuthenticationError):
            verify_request_envelope(
                envelope,
                TOKEN,
                expected_action="start",
                expected_device_id=DEVICE,
                expected_peer_ip="192.0.2.99",
            )
        for address in ("192.000.2.10", "192.0.2.256", "::1", "192.0.2.10 "):
            with self.subTest(address=address), self.assertRaises(CompanionProtocolError):
                request_envelope(
                    TOKEN, "start", DEVICE, address, OPERATION, 7,
                    123456789, NONCE,
                )

    def test_request_tamper_replay_and_cross_device_are_rejected(self) -> None:
        original = request_envelope(
            TOKEN, "status", DEVICE, DEVICE_IP, OPERATION, 7, 123456789,
            NONCE,
        )
        for field, value in (
            ("device_id", "pj-other"),
            ("device_ip", "192.0.2.11"),
            ("operation_id", "pj-test-00000008"),
            ("generation", 8),
            ("requested_ms", 123456790),
            ("action", "start"),
            ("nonce", "ffeeddccbbaa99887766554433221100"),
        ):
            tampered = copy.deepcopy(original)
            tampered[field] = value
            with self.subTest(field=field), self.assertRaises(
                (CompanionAuthenticationError, CompanionProtocolError)
            ):
                verify_request_envelope(
                    tampered,
                    TOKEN,
                    expected_action="status",
                    expected_device_id=DEVICE,
                    expected_peer_ip=DEVICE_IP,
                )
        for field, value in (("version", True), ("action", ["status"])):
            malformed = copy.deepcopy(original)
            malformed[field] = value
            with self.subTest(field=field), self.assertRaises(CompanionProtocolError):
                verify_request_envelope(
                    malformed, TOKEN, expected_action="status",
                    expected_device_id=DEVICE, expected_peer_ip=DEVICE_IP,
                )

    def test_response_authenticates_exact_progress_and_identity(self) -> None:
        snapshot = {
            "device_id": DEVICE,
            "operation_id": OPERATION,
            "generation": 7,
            "requested_ms": 123456789,
            "state": "failed",
            "total": 4,
            "pending": 1,
            "transferred": 2,
            "failed": 1,
            "error": "One recording failed",
        }
        envelope = response_envelope(
            snapshot, TOKEN, action="start", nonce=NONCE
        )
        self.assertEqual(
            envelope["mac"],
            "750053ebe806b0ca64a6cb3140bc724b4c430f6e835118b432468cc3639c96c5",
        )
        verify_response_envelope(
            envelope,
            TOKEN,
            expected_device_id=DEVICE,
            expected_operation_id=OPERATION,
            expected_generation=7,
            expected_requested_ms=123456789,
            expected_action="start",
            expected_nonce=NONCE,
        )
        for field, value in (
            ("device_id", "pj-other"),
            ("operation_id", "pj-test-00000008"),
            ("generation", 8),
            ("requested_ms", 123456790),
            ("state", "succeeded"),
            ("total", 5),
            ("pending", 0),
            ("transferred", 3),
            ("failed", 0),
            ("error", "Different"),
            ("action", "status"),
            ("nonce", "ffeeddccbbaa99887766554433221100"),
        ):
            tampered = copy.deepcopy(envelope)
            tampered[field] = value
            with self.subTest(field=field), self.assertRaises(
                (CompanionAuthenticationError, CompanionProtocolError)
            ):
                verify_response_envelope(tampered, TOKEN)

    def test_inconsistent_progress_and_stale_response_fail_closed(self) -> None:
        with self.assertRaises(CompanionProtocolError):
            response_envelope({
                "device_id": DEVICE,
                "operation_id": OPERATION,
                "generation": 7,
                "requested_ms": 123456789,
                "state": "running",
                "total": 4,
                "pending": 2,
                "transferred": 2,
                "failed": 1,
                "error": "",
            }, TOKEN, action="status", nonce=NONCE)
        valid = response_envelope({
            "device_id": DEVICE,
            "operation_id": OPERATION,
            "generation": 7,
            "requested_ms": 123456789,
            "state": "running",
            "total": 4,
            "pending": 2,
            "transferred": 2,
            "failed": 0,
            "error": "",
        }, TOKEN, action="status", nonce=NONCE)
        with self.assertRaises(CompanionAuthenticationError):
            verify_response_envelope(
                valid, TOKEN, expected_device_id=DEVICE,
                expected_operation_id=OPERATION, expected_generation=7,
                expected_requested_ms=123456788,
                expected_action="status", expected_nonce=NONCE,
            )
        with self.assertRaises(CompanionAuthenticationError):
            verify_response_envelope(
                valid, TOKEN, expected_device_id=DEVICE,
                expected_operation_id=OPERATION, expected_generation=7,
                expected_requested_ms=123456789,
                expected_action="status",
                expected_nonce="ffeeddccbbaa99887766554433221100",
            )
        for field, value in (("version", True), ("action", ["status"]),
                             ("state", ["running"])):
            malformed = copy.deepcopy(valid)
            malformed[field] = value
            with self.subTest(field=field), self.assertRaises(CompanionProtocolError):
                verify_response_envelope(malformed, TOKEN)

    def test_default_token_and_unsafe_errors_are_never_used(self) -> None:
        class MaliciousError:
            def __str__(self) -> str:
                raise RuntimeError("cannot format")

        self.assertFalse(provisioned_token(""))
        self.assertFalse(provisioned_token("dev-token"))
        self.assertFalse(provisioned_token("short"))
        with self.assertRaises(CompanionAuthenticationError):
            request_envelope(
                "dev-token", "start", DEVICE, DEVICE_IP, OPERATION, 7,
                123456789, NONCE,
            )
        normalized = normalize_error("bad\n\x00snowman \u2603 " + "x" * 200)
        self.assertLessEqual(len(normalized.encode("utf-8")), 127)
        self.assertTrue(normalized.isascii())
        self.assertNotIn("\n", normalized)
        self.assertNotIn("\x00", normalized)
        self.assertEqual(normalize_error(MaliciousError(), "\n\u2603"), "Sync failed")

    def test_data_credential_is_domain_separated_and_operation_scoped(self) -> None:
        scoped = scoped_data_token(TOKEN, DEVICE, OPERATION, 7, 123456789)
        self.assertEqual(
            scoped,
            "abd23c23cf3c885614ec9ccda61bc8390901642c25cf1ade60bc16fa40e31681",
        )
        self.assertNotIn(TOKEN, scoped)
        self.assertNotEqual(
            scoped,
            scoped_data_token(TOKEN, DEVICE, OPERATION, 8, 123456789),
        )
        self.assertEqual(len(pairing_epoch(TOKEN)), 64)
        self.assertNotEqual(pairing_epoch(TOKEN), pairing_epoch(TOKEN + "-new"))
        self.assertNotEqual(pairing_epoch(TOKEN), scoped)
        self.assertEqual(pairing_epoch("dev-token"), "")


if __name__ == "__main__":
    unittest.main()
