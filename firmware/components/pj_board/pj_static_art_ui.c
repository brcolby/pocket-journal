#include "pj_static_art_ui.h"

void pj_static_art_publish_to_ui(pj_ui_context_t *ui, const pj_static_art_t *art)
{
    if (ui == NULL) {
        return;
    }
    if (art != NULL) {
        pj_ui_set_static_art(ui, art->pixels, sizeof(art->pixels));
    } else {
        pj_ui_clear_static_art(ui);
    }
}
