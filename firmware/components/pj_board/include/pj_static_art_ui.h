#pragma once

#include "pj_static_art.h"
#include "pj_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void pj_static_art_publish_to_ui(pj_ui_context_t *ui, const pj_static_art_t *art);

#ifdef __cplusplus
}
#endif
