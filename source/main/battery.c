#include "battery.h"
#include "board.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#define SAMPLES 16

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_ch;

bool battery_init(void)
{
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(BAT_ADC, &unit, &s_ch) != ESP_OK) {
        return false;
    }
    adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = unit};
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) {
        s_adc = NULL;
        return false;
    }
    adc_oneshot_chan_cfg_t ccfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_oneshot_config_channel(s_adc, s_ch, &ccfg);
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = unit,
        .chan = s_ch,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        s_cali = NULL;
    }
    return true;
}

int battery_mv(void)
{
    if (!s_adc) {
        return 0;
    }
    int sum = 0, n = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, s_ch, &raw) != ESP_OK) {
            continue;
        }
        if (!s_cali || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
            mv = raw * 3100 / 4095; /* uncalibrated, 12 dB full scale */
        }
        sum += mv;
        n++;
    }
    return n ? sum / n * BAT_DIVIDER : 0;
}

int battery_pct(int mv)
{
    static const struct {
        int mv, pct;
    } CURVE[] = {
        {4180, 100}, {4100, 90}, {4000, 78}, {3900, 64}, {3800, 50},
        {3700, 32}, {3600, 15}, {3500, 6}, {3300, 0},
    };
    const int n = sizeof(CURVE) / sizeof(CURVE[0]);
    if (mv >= CURVE[0].mv) {
        return 100;
    }
    for (int i = 1; i < n; i++) {
        if (mv >= CURVE[i].mv) {
            return CURVE[i].pct + (mv - CURVE[i].mv) * (CURVE[i - 1].pct - CURVE[i].pct) /
                                      (CURVE[i - 1].mv - CURVE[i].mv);
        }
    }
    return 0;
}
