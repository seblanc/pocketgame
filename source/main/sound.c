#include "sound.h"
#include <math.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "i2cbus.h"

#define RATE 16000 /* MCLK = 256 * RATE = 4.096 MHz */
#define AMPLITUDE 6000
#define DAC_VOLUME 0xB4 /* ES8311 reg 0x32: 0xBF = 0 dB, 0.5 dB steps */

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

static void play_note(const note_t *n)
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

static void sound_task(void *arg)
{
    sound_t s;
    for (;;) {
        if (xQueueReceive(s_q, &s, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        for (const note_t *n = TUNES[s]; n->ms; n++) {
            play_note(n);
        }
    }
}

/* ES8311 as I2S slave, MCLK from its pin at 256 x fs, 16-bit I2S, DAC to the
 * speaker amplifier. Same register sequence as Espressif's es8311 driver. */
static bool es8311_start(void)
{
    static const uint8_t SEQ[][2] = {
        {0x44, 0x08}, /* I2C noise immunity (written twice in the reference) */
        {0x44, 0x08},
        {0x01, 0x30},
        {0x02, 0x00},
        {0x03, 0x10},
        {0x16, 0x24},
        {0x04, 0x10},
        {0x05, 0x00},
        {0x0B, 0x00},
        {0x0C, 0x00},
        {0x10, 0x1F},
        {0x11, 0x7F},
        {0x00, 0x80}, /* chip state machine on, slave mode */
        {0x01, 0x3F}, /* all clocks on, MCLK from the MCLK pin */
        {0x02, 0x00}, /* pre-divider 1, multiplier 1 */
        {0x05, 0x00}, /* ADC/DAC clock dividers 1 */
        {0x03, 0x10}, /* single speed, ADC OSR */
        {0x04, 0x10}, /* DAC OSR */
        {0x07, 0x00},
        {0x08, 0xFF},
        {0x06, 0x03},
        {0x09, 0x0C}, /* serial in: I2S, 16 bit */
        {0x0A, 0x0C}, /* serial out: I2S, 16 bit */
        {0x13, 0x10},
        {0x1B, 0x0A},
        {0x1C, 0x6A},
        {0x0D, 0x01}, /* power up analog */
        {0x0E, 0x02},
        {0x12, 0x00}, /* power up the DAC */
        {0x14, 0x1A},
        {0x15, 0x40},
        {0x37, 0x08}, /* bypass the DAC equalizer */
        {0x45, 0x00},
        {0x32, DAC_VOLUME},
        {0x31, 0x00}, /* unmute */
    };
    for (size_t i = 0; i < sizeof(SEQ) / sizeof(SEQ[0]); i++) {
        if (!i2cbus_write_reg(ES8311_ADDR, SEQ[i][0], SEQ[i][1])) {
            return false;
        }
    }
    return true;
}

const char *sound_init(void)
{
    gpio_reset_pin(PA_CTRL);
    gpio_set_direction(PA_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_level(PA_CTRL, 0);

    /* MCLK must run before the codec is configured */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_new_channel(&cc, &s_tx, NULL) != ESP_OK || i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK ||
        i2s_channel_enable(s_tx) != ESP_OK) {
        s_tx = NULL;
        return "none (i2s failed)";
    }
    if (!es8311_start()) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return "none (es8311 did not answer)";
    }
    gpio_set_level(PA_CTRL, 1);

    s_q = xQueueCreate(4, sizeof(sound_t));
    xTaskCreate(sound_task, "sound", 3072, NULL, 4, NULL);
    return "es8311";
}

void sound_play(sound_t s)
{
    if (s_q) {
        xQueueSend(s_q, &s, 0);
    }
}
