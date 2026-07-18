# Third-Party Notices

Pocket Journal's original firmware, partner application, simulator, tools, and
documentation are released under the repository's [MIT License](LICENSE).
The following bundled assets retain their upstream licenses:

- Carbon icons in `assets/icons/carbon/` are from `@carbon/icons` 11.82.0 and
  are licensed under Apache License 2.0. The complete upstream text is in
  `assets/icons/carbon/LICENSE`; source and integrity details are in
  `assets/icons/carbon/SOURCE.md`.
- IBM Plex Mono Bold in `assets/fonts/` is licensed under the SIL Open Font
  License 1.1. The complete upstream text is in
  `assets/fonts/IBMPlexMono-LICENSE.txt`; source and integrity details are in
  `assets/fonts/IBMPlexMono-SOURCE.md`.

The release firmware is built with ESP-IDF 6.0.1. Its locked direct managed
components are cJSON 1.7.19~2 (MIT), Espressif `esp_codec_dev` 1.5.10
(Apache-2.0), and Espressif mDNS 1.11.3 (Apache-2.0). The firmware release
bundle includes an SPDX software bill of materials generated from the clean
ESP-IDF build for the complete linked-component inventory.

Dependencies fetched by ESP-IDF or Python package managers are not vendored in
the release source tree. Their own license terms continue to apply.
