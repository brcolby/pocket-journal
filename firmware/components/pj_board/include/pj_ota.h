#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pj_ota_mutation_reserve_fn)(void);
typedef void (*pj_ota_mutation_release_fn)(void);
typedef int (*pj_ota_token_read_fn)(char *token, size_t capacity);

void pj_ota_init(const char *device_id, pj_ota_token_read_fn read_token,
                 const char *board,
                 pj_ota_mutation_reserve_fn reserve_mutations,
                 pj_ota_mutation_release_fn release_mutations);
esp_err_t pj_ota_register_http(httpd_handle_t server);
void pj_ota_confirm_boot_health(int healthy);
int pj_ota_write_enabled(void);

#ifdef __cplusplus
}
#endif
