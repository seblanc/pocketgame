#include "sound.h"
#include <math.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define RATE 16000
#define AMPLITUDE 6000

typedef struct {
    uint16_t hz, ms;
} note_t;

static const note_t TUNE_X[] = {{660, 60}, {0, 0}};
static const note_t TUNE_O[] = {{520, 60}, {0, 0}};
static const note_t TUNE_BAD[] = {{180, 140}, {0, 0}};
static const note_t TUNE_WIN[] = {{523, 110}, {659, 110}, {784, 110}, {1047, 240}, {0, 0}};
static const note_t TUNE_DRAW[] = {{440, 160}, {330, 260}, {0, 0}};
static const note_t TUNE_NEW[] = {{880, 50}, {1175, 70}, {0, 0}};
static const note_t *const TUNES[] = {TUNE_X, TUNE_O, TUNE_BAD, TUNE_WIN, TUNE_DRAW, TUNE_NEW};

static QueueHandle_t s_q;
static i2s_chan_handle_t s_tx;
static int s_buzzer = -1;

static void i2s_note(const note_t *n)
{
    static int16_t buf[256 * 2];
    int total = RATE * n->ms / 1000, done = 0;
    float phase = 0, step = 2 * (float)M_PI * n->hz / RATE;
    int fade = RATE / 200; /* 5 ms ramps avoid clicks */
    while (done < total) {
        int frames = total - done > 256 ? 256 : total - done;
        for (int i = 0; i < frames; i++, done++) {
            float env = 1;
            if (done < fade) {
                env = (float)done / fade;
            } else if (total - done < fade) {
                env = (float)(total - done) / fade;
            }
            int16_t s = (int16_t)(sinf(phase) * AMPLITUDE * env);
            phase += step;
            if (phase > 2 * (float)M_PI) {
                phase -= 2 * (float)M_PI;
            }
            buf[2 * i] = buf[2 * i + 1] = s;
        }
        size_t written;
        i2s_channel_write(s_tx, buf, frames * 4, &written, pdMS_TO_TICKS(500));
    }
}

static void buzzer_note(const note_t *n)
{
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, n->hz);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(n->ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void sound_task(void *arg)
{
    sound_t s;
    for (;;) {
        if (xQueueReceive(s_q, &s, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        for (const note_t *n = TUNES[s]; n->ms; n++) {
            if (s_tx) {
                i2s_note(n);
            } else if (s_buzzer >= 0) {
                buzzer_note(n);
            }
        }
    }
}

const char *sound_init(const board_profile_t *p)
{
    const char *kind = "none";
    if (p->i2s_dout >= 0) {
        i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        cc.auto_clear = true;
        i2s_std_config_t sc = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = p->i2s_bclk,
                .ws = p->i2s_ws,
                .dout = p->i2s_dout,
                .din = I2S_GPIO_UNUSED,
            },
        };
        if (i2s_new_channel(&cc, &s_tx, NULL) == ESP_OK && i2s_channel_init_std_mode(s_tx, &sc) == ESP_OK &&
            i2s_channel_enable(s_tx) == ESP_OK) {
            kind = "i2s";
            if (p->amp_en >= 0) {
                gpio_reset_pin(p->amp_en);
                gpio_set_direction(p->amp_en, GPIO_MODE_OUTPUT);
                gpio_set_level(p->amp_en, 1);
            }
        } else {
            s_tx = NULL;
        }
    } else if (p->buzzer >= 0) {
        ledc_timer_config_t t = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 1000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_channel_config_t c = {
            .gpio_num = p->buzzer,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
        };
        if (ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&c) == ESP_OK) {
            s_buzzer = p->buzzer;
            kind = "buzzer";
        }
    }
    s_q = xQueueCreate(4, sizeof(sound_t));
    xTaskCreate(sound_task, "sound", 3072, NULL, 4, NULL);
    return kind;
}

void sound_play(sound_t s)
{
    if (s_q) {
        xQueueSend(s_q, &s, 0);
    }
}
