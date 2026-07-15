#include "pj_display_worker.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

static pj_ui_dirty_region_t full_region(void)
{
    return (pj_ui_dirty_region_t) {
        .x = 0,
        .y = 0,
        .width = PJ_DISPLAY_WIDTH,
        .height = PJ_DISPLAY_HEIGHT,
        .partial = 0,
    };
}

static int normalize_region(const pj_ui_dirty_region_t *input,
                            pj_ui_dirty_region_t *output)
{
    if (input == NULL || output == NULL ||
        input->width <= 0 || input->height <= 0) {
        return 0;
    }
    if (!input->partial) {
        *output = full_region();
        return 1;
    }

    int64_t x0 = input->x;
    int64_t y0 = input->y;
    int64_t x1 = x0 + input->width;
    int64_t y1 = y0 + input->height;
    if (x1 <= 0 || y1 <= 0 ||
        x0 >= PJ_DISPLAY_WIDTH || y0 >= PJ_DISPLAY_HEIGHT) {
        return 0;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PJ_DISPLAY_WIDTH) x1 = PJ_DISPLAY_WIDTH;
    if (y1 > PJ_DISPLAY_HEIGHT) y1 = PJ_DISPLAY_HEIGHT;
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    *output = (pj_ui_dirty_region_t) {
        .x = (int)x0,
        .y = (int)y0,
        .width = (int)(x1 - x0),
        .height = (int)(y1 - y0),
        .partial = 1,
    };
    return 1;
}

static pj_ui_dirty_region_t merge_regions(const pj_ui_dirty_region_t *left,
                                           const pj_ui_dirty_region_t *right)
{
    pj_ui_dirty_region_t a;
    pj_ui_dirty_region_t b;
    if (!normalize_region(left, &a)) {
        return normalize_region(right, &b) ? b : full_region();
    }
    if (!normalize_region(right, &b)) {
        return a;
    }
    if (!a.partial || !b.partial) {
        return full_region();
    }
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int a_x1 = a.x + a.width;
    int b_x1 = b.x + b.width;
    int a_y1 = a.y + a.height;
    int b_y1 = b.y + b.height;
    int x1 = a_x1 > b_x1 ? a_x1 : b_x1;
    int y1 = a_y1 > b_y1 ? a_y1 : b_y1;
    return (pj_ui_dirty_region_t) {
        .x = x0,
        .y = y0,
        .width = x1 - x0,
        .height = y1 - y0,
        .partial = 1,
    };
}

static void clear_slot(pj_display_worker_slot_t *slot)
{
    memset(slot, 0, sizeof(*slot));
}

static int latest_ready_slot(const pj_display_worker_model_t *model)
{
    int latest = -1;
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_READY) {
            continue;
        }
        if (latest < 0 ||
            model->slots[i].generation > model->slots[latest].generation) {
            latest = i;
        }
    }
    return latest;
}

static void coalesce_ready_slots(pj_display_worker_model_t *model, int latest)
{
    if (latest < 0) {
        return;
    }
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (i == latest ||
            model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_READY) {
            continue;
        }
        model->slots[latest].dirty = merge_regions(
            &model->slots[latest].dirty, &model->slots[i].dirty);
        clear_slot(&model->slots[i]);
    }
}

void pj_display_worker_model_init(pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->next_generation = 1;
    model->accepting = 1;
}

int pj_display_worker_model_begin_submit(pj_display_worker_model_t *model,
                                         int *slot_index)
{
    if (model == NULL || slot_index == NULL || !model->accepting) {
        return 0;
    }

    int slot = latest_ready_slot(model);
    coalesce_ready_slots(model, slot);
    if (slot < 0) {
        for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
            if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_FREE) {
                slot = i;
                clear_slot(&model->slots[i]);
                break;
            }
        }
    }
    if (slot < 0) {
        return 0;
    }
    model->slots[slot].state = PJ_DISPLAY_WORKER_SLOT_WRITING;
    *slot_index = slot;
    return 1;
}

int pj_display_worker_model_commit_submit(pj_display_worker_model_t *model,
                                          int slot_index,
                                          const pj_ui_dirty_region_t *dirty)
{
    pj_ui_dirty_region_t normalized;
    if (model == NULL || slot_index < 0 ||
        slot_index >= PJ_DISPLAY_WORKER_SLOT_COUNT ||
        model->slots[slot_index].state != PJ_DISPLAY_WORKER_SLOT_WRITING) {
        return 0;
    }
    if (!model->accepting || !normalize_region(dirty, &normalized)) {
        clear_slot(&model->slots[slot_index]);
        return 0;
    }

    pj_display_worker_slot_t *slot = &model->slots[slot_index];
    if (model->force_full_on_commit) {
        slot->dirty = full_region();
        model->force_full_on_commit = 0;
    } else if (slot->dirty.width > 0 && slot->dirty.height > 0) {
        slot->dirty = merge_regions(&slot->dirty, &normalized);
    } else {
        slot->dirty = normalized;
    }
    slot->generation = model->next_generation++;
    if (model->next_generation == 0) {
        model->next_generation = 1;
    }
    slot->state = PJ_DISPLAY_WORKER_SLOT_READY;
    return 1;
}

int pj_display_worker_model_take(pj_display_worker_model_t *model,
                                 int *slot_index,
                                 pj_ui_dirty_region_t *dirty,
                                 uint32_t *generation)
{
    if (model == NULL || slot_index == NULL || dirty == NULL) {
        return 0;
    }
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_DISPLAYING) {
            return 0;
        }
    }

    int slot = latest_ready_slot(model);
    if (slot < 0) {
        return 0;
    }
    coalesce_ready_slots(model, slot);
    model->slots[slot].state = PJ_DISPLAY_WORKER_SLOT_DISPLAYING;
    *slot_index = slot;
    *dirty = model->slots[slot].dirty;
    if (generation != NULL) {
        *generation = model->slots[slot].generation;
    }
    return 1;
}

void pj_display_worker_model_complete(pj_display_worker_model_t *model,
                                      int slot_index,
                                      int success)
{
    if (model == NULL || slot_index < 0 ||
        slot_index >= PJ_DISPLAY_WORKER_SLOT_COUNT ||
        model->slots[slot_index].state != PJ_DISPLAY_WORKER_SLOT_DISPLAYING) {
        return;
    }
    if (success || !model->accepting) {
        clear_slot(&model->slots[slot_index]);
        return;
    }

    int ready = latest_ready_slot(model);
    if (ready >= 0) {
        coalesce_ready_slots(model, ready);
        model->slots[ready].dirty = full_region();
        clear_slot(&model->slots[slot_index]);
        return;
    }
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state == PJ_DISPLAY_WORKER_SLOT_WRITING) {
            model->force_full_on_commit = 1;
            clear_slot(&model->slots[slot_index]);
            return;
        }
    }

    model->slots[slot_index].dirty = full_region();
    model->slots[slot_index].state = PJ_DISPLAY_WORKER_SLOT_READY;
}

void pj_display_worker_model_shutdown(pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return;
    }
    model->accepting = 0;
    model->force_full_on_commit = 0;
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_DISPLAYING) {
            clear_slot(&model->slots[i]);
        }
    }
}

int pj_display_worker_model_is_idle(const pj_display_worker_model_t *model)
{
    if (model == NULL) {
        return 1;
    }
    for (int i = 0; i < PJ_DISPLAY_WORKER_SLOT_COUNT; i++) {
        if (model->slots[i].state != PJ_DISPLAY_WORKER_SLOT_FREE) {
            return 0;
        }
    }
    return 1;
}

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pj_board.h"

#define PJ_DISPLAY_WORKER_STACK_WORDS 4096
#define PJ_DISPLAY_WORKER_PRIORITY 3
#define PJ_DISPLAY_WORKER_RETRY_MIN_MS 250U
#define PJ_DISPLAY_WORKER_RETRY_MAX_MS 2000U

static const char *TAG = "pj-display-worker";
static pj_display_worker_model_t g_model;
static pj_framebuffer_t g_slots[PJ_DISPLAY_WORKER_SLOT_COUNT];
static StaticTask_t g_task_storage;
static StackType_t g_task_stack[PJ_DISPLAY_WORKER_STACK_WORDS];
static TaskHandle_t g_task;
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
static int g_stop_requested;
static uint32_t g_committed_frames;

static uint32_t retry_delay_ms(uint32_t failures)
{
    uint32_t delay = PJ_DISPLAY_WORKER_RETRY_MIN_MS;
    while (failures > 1 && delay < PJ_DISPLAY_WORKER_RETRY_MAX_MS) {
        delay *= 2U;
        failures--;
    }
    return delay > PJ_DISPLAY_WORKER_RETRY_MAX_MS ?
        PJ_DISPLAY_WORKER_RETRY_MAX_MS : delay;
}

static void display_worker_task(void *argument)
{
    (void)argument;
    TickType_t wait_ticks = portMAX_DELAY;
    uint32_t consecutive_failures = 0;

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks);

        int slot = -1;
        int stopping;
        pj_ui_dirty_region_t dirty;
        uint32_t generation = 0;
        portENTER_CRITICAL(&g_lock);
        stopping = g_stop_requested;
        int ready = !stopping && pj_display_worker_model_take(
            &g_model, &slot, &dirty, &generation);
        portEXIT_CRITICAL(&g_lock);
        if (stopping) {
            break;
        }
        if (!ready) {
            wait_ticks = portMAX_DELAY;
            continue;
        }

        int success = pj_board_display_framebuffer(&g_slots[slot], &dirty);
        portENTER_CRITICAL(&g_lock);
        pj_display_worker_model_complete(&g_model, slot, success);
        if (success && g_committed_frames < UINT32_MAX) {
            g_committed_frames++;
        }
        stopping = g_stop_requested;
        portEXIT_CRITICAL(&g_lock);

        if (stopping) {
            break;
        }
        if (success) {
            consecutive_failures = 0;
            wait_ticks = 0;
        } else {
            if (consecutive_failures < UINT32_MAX) {
                consecutive_failures++;
            }
            uint32_t delay_ms = retry_delay_ms(consecutive_failures);
            ESP_LOGW(TAG,
                     "Display generation %" PRIu32
                     " failed; latest frame will retry full in %" PRIu32 " ms",
                     generation, delay_ms);
            wait_ticks = pdMS_TO_TICKS(delay_ms);
        }
    }

    portENTER_CRITICAL(&g_lock);
    pj_display_worker_model_shutdown(&g_model);
    g_task = NULL;
    portEXIT_CRITICAL(&g_lock);
    vTaskDelete(NULL);
}

int pj_display_worker_start(void)
{
    portENTER_CRITICAL(&g_lock);
    if (g_task != NULL) {
        int running = !g_stop_requested;
        portEXIT_CRITICAL(&g_lock);
        return running;
    }
    memset(g_slots, 0, sizeof(g_slots));
    pj_display_worker_model_init(&g_model);
    g_stop_requested = 0;
    g_committed_frames = 0;
    portEXIT_CRITICAL(&g_lock);

    TaskHandle_t task = xTaskCreateStatic(
        display_worker_task, "pj-display", PJ_DISPLAY_WORKER_STACK_WORDS,
        NULL, PJ_DISPLAY_WORKER_PRIORITY, g_task_stack, &g_task_storage);
    if (task == NULL) {
        portENTER_CRITICAL(&g_lock);
        pj_display_worker_model_shutdown(&g_model);
        portEXIT_CRITICAL(&g_lock);
        ESP_LOGE(TAG, "Failed to create display worker task");
        return 0;
    }
    portENTER_CRITICAL(&g_lock);
    g_task = task;
    portEXIT_CRITICAL(&g_lock);
    return 1;
}

void pj_display_worker_stop(void)
{
    TaskHandle_t task;
    portENTER_CRITICAL(&g_lock);
    g_stop_requested = 1;
    pj_display_worker_model_shutdown(&g_model);
    task = g_task;
    portEXIT_CRITICAL(&g_lock);
    if (task != NULL) {
        xTaskNotifyGive(task);
    }
}

int pj_display_worker_submit(const pj_framebuffer_t *framebuffer,
                             const pj_ui_dirty_region_t *dirty)
{
    if (framebuffer == NULL || dirty == NULL) {
        return 0;
    }

    int slot = -1;
    portENTER_CRITICAL(&g_lock);
    int accepted = g_task != NULL && !g_stop_requested &&
        pj_display_worker_model_begin_submit(&g_model, &slot);
    portEXIT_CRITICAL(&g_lock);
    if (!accepted) {
        return 0;
    }

    memcpy(&g_slots[slot], framebuffer, sizeof(*framebuffer));

    TaskHandle_t task;
    portENTER_CRITICAL(&g_lock);
    accepted = pj_display_worker_model_commit_submit(&g_model, slot, dirty);
    task = g_task;
    portEXIT_CRITICAL(&g_lock);
    if (!accepted || task == NULL) {
        return 0;
    }
    xTaskNotifyGive(task);
    return 1;
}

int pj_display_worker_is_idle(void)
{
    portENTER_CRITICAL(&g_lock);
    int idle = g_task != NULL && !g_stop_requested &&
        pj_display_worker_model_is_idle(&g_model);
    portEXIT_CRITICAL(&g_lock);
    return idle;
}

uint32_t pj_display_worker_committed_frames(void)
{
    portENTER_CRITICAL(&g_lock);
    uint32_t committed = g_committed_frames;
    portEXIT_CRITICAL(&g_lock);
    return committed;
}

#else

int pj_display_worker_start(void)
{
    return 0;
}

void pj_display_worker_stop(void)
{
}

int pj_display_worker_submit(const pj_framebuffer_t *framebuffer,
                             const pj_ui_dirty_region_t *dirty)
{
    (void)framebuffer;
    (void)dirty;
    return 0;
}

int pj_display_worker_is_idle(void)
{
    return 1;
}

uint32_t pj_display_worker_committed_frames(void)
{
    return 0;
}

#endif
