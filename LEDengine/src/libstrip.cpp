#include "libstrip.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sys/cdefs.h>

extern "C" {
#include "sdkconfig.h"
}

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "LibStrip";

struct le_led_strip_t;
using le_led_strip_handle_t = le_led_strip_t*;

enum le_led_model_t {
    LE_LED_MODEL_WS2812,
    LE_LED_MODEL_SK6812,
    LE_LED_MODEL_WS2811,
    LE_LED_MODEL_INVALID
};

struct le_led_strip_encoder_timings_t {
    uint32_t t0h;
    uint32_t t1h;
    uint32_t t0l;
    uint32_t t1l;
    uint32_t reset;
};

union le_led_color_component_format_t {
    struct {
        uint32_t r_pos : 2;
        uint32_t g_pos : 2;
        uint32_t b_pos : 2;
        uint32_t w_pos : 2;
        uint32_t reserved : 21;
        uint32_t num_components : 3;
    } format;
    uint32_t format_id;
};

struct le_led_strip_config_t {
    int strip_gpio_num;
    uint32_t max_leds;
    le_led_model_t led_model;
    le_led_color_component_format_t color_component_format;
    uint8_t* external_pixel_buf;
    struct {
        uint32_t invert_out : 1;
    } flags;
    le_led_strip_encoder_timings_t timings;
};

struct le_led_strip_rmt_config_t {
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    struct {
        uint32_t with_dma : 1;
    } flags;
    uint8_t interrupt_priority;
};

typedef struct {
    uint32_t resolution;
    le_led_model_t led_model;
    le_led_strip_encoder_timings_t timings;
} le_led_strip_encoder_config_t;

struct le_led_strip_t {
    esp_err_t (*set_pixel)(le_led_strip_t* strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue);
    esp_err_t (*set_pixel_rgbw)(le_led_strip_t* strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white);
    esp_err_t (*refresh)(le_led_strip_t* strip);
    esp_err_t (*clear)(le_led_strip_t* strip);
    esp_err_t (*del)(le_led_strip_t* strip);
};

struct LedStripRmtObj {
    le_led_strip_t base;
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t strip_encoder;
    rmt_transmit_config_t tx_conf;
    uint32_t strip_len;
    uint8_t bytes_per_pixel;
    le_led_color_component_format_t component_fmt;
    uint8_t* pixel_buf;
    bool pixel_buf_allocated_internally;
};

inline LedStripRmtObj* toRmt(le_led_strip_t* strip) {
    return reinterpret_cast<LedStripRmtObj*>(strip);
}

esp_err_t le_rmt_new_led_strip_encoder(const le_led_strip_encoder_config_t* config, rmt_encoder_handle_t* ret_encoder);

static esp_err_t led_strip_rmt_set_pixel(le_led_strip_t* strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue) {
    auto* rmt_strip = toRmt(strip);
    ESP_RETURN_ON_FALSE(index < rmt_strip->strip_len, ESP_ERR_INVALID_ARG, kTag, "index out of range");

    le_led_color_component_format_t component_fmt = rmt_strip->component_fmt;
    uint32_t start = index * rmt_strip->bytes_per_pixel;
    uint8_t* pixel_buf = rmt_strip->pixel_buf;

    pixel_buf[start + component_fmt.format.r_pos] = red & 0xFF;
    pixel_buf[start + component_fmt.format.g_pos] = green & 0xFF;
    pixel_buf[start + component_fmt.format.b_pos] = blue & 0xFF;
    if (component_fmt.format.num_components > 3) {
        pixel_buf[start + component_fmt.format.w_pos] = 0;
    }

    return ESP_OK;
}

static esp_err_t led_strip_rmt_set_pixel_rgbw(le_led_strip_t* strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue, uint32_t white) {
    auto* rmt_strip = toRmt(strip);
    le_led_color_component_format_t component_fmt = rmt_strip->component_fmt;
    ESP_RETURN_ON_FALSE(index < rmt_strip->strip_len, ESP_ERR_INVALID_ARG, kTag, "index out of range");
    ESP_RETURN_ON_FALSE(component_fmt.format.num_components == 4, ESP_ERR_INVALID_ARG, kTag, "strip lacks white component");

    uint32_t start = index * rmt_strip->bytes_per_pixel;
    uint8_t* pixel_buf = rmt_strip->pixel_buf;

    pixel_buf[start + component_fmt.format.r_pos] = red & 0xFF;
    pixel_buf[start + component_fmt.format.g_pos] = green & 0xFF;
    pixel_buf[start + component_fmt.format.b_pos] = blue & 0xFF;
    pixel_buf[start + component_fmt.format.w_pos] = white & 0xFF;

    return ESP_OK;
}

static esp_err_t led_strip_rmt_refresh(le_led_strip_t* strip) {
    auto* rmt_strip = toRmt(strip);
    ESP_RETURN_ON_ERROR(rmt_enable(rmt_strip->rmt_chan), kTag, "enable channel failed");
    ESP_RETURN_ON_ERROR(rmt_transmit(rmt_strip->rmt_chan, rmt_strip->strip_encoder, rmt_strip->pixel_buf,
                                     rmt_strip->strip_len * rmt_strip->bytes_per_pixel, &rmt_strip->tx_conf), kTag,
                        "transmit failed");
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(rmt_strip->rmt_chan, -1), kTag, "wait done failed");
    ESP_RETURN_ON_ERROR(rmt_disable(rmt_strip->rmt_chan), kTag, "disable channel failed");
    return ESP_OK;
}

static esp_err_t led_strip_rmt_clear(le_led_strip_t* strip) {
    auto* rmt_strip = toRmt(strip);
    memset(rmt_strip->pixel_buf, 0, rmt_strip->strip_len * rmt_strip->bytes_per_pixel);
    return led_strip_rmt_refresh(strip);
}

static esp_err_t led_strip_rmt_del(le_led_strip_t* strip) {
    auto* rmt_strip = toRmt(strip);
    ESP_RETURN_ON_ERROR(rmt_del_channel(rmt_strip->rmt_chan), kTag, "delete channel failed");
    ESP_RETURN_ON_ERROR(rmt_del_encoder(rmt_strip->strip_encoder), kTag, "delete encoder failed");
    if (rmt_strip->pixel_buf_allocated_internally) {
        free(rmt_strip->pixel_buf);
    }
    free(rmt_strip);
    return ESP_OK;
}

esp_err_t le_led_strip_set_pixel(le_led_strip_handle_t strip, uint32_t index, uint32_t red, uint32_t green, uint32_t blue) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, kTag, "invalid strip");
    return strip->set_pixel(strip, index, red, green, blue);
}

esp_err_t le_led_strip_set_pixel_rgbw(le_led_strip_handle_t strip, uint32_t index, uint32_t red, uint32_t green,
                                      uint32_t blue, uint32_t white) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, kTag, "invalid strip");
    return strip->set_pixel_rgbw(strip, index, red, green, blue, white);
}

esp_err_t le_led_strip_refresh(le_led_strip_handle_t strip) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, kTag, "invalid strip");
    return strip->refresh(strip);
}

esp_err_t le_led_strip_clear(le_led_strip_handle_t strip) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, kTag, "invalid strip");
    return strip->clear(strip);
}

esp_err_t le_led_strip_del(le_led_strip_handle_t strip) {
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, kTag, "invalid strip");
    return strip->del(strip);
}

le_led_color_component_format_t colorFormatForOrder(led_order order, int bytesPerPixel) {
    le_led_color_component_format_t fmt = {};
    fmt.format.num_components = bytesPerPixel;

    auto assignPositions = [&](uint8_t first, uint8_t second, uint8_t third) {
        fmt.format.r_pos = first;
        fmt.format.g_pos = second;
        fmt.format.b_pos = third;
        fmt.format.w_pos = 3;
    };

    switch (order) {
        case S_RGB:
            assignPositions(0, 1, 2);
            break;
        case S_RBG:
            assignPositions(0, 2, 1);
            break;
        case S_GRB:
            assignPositions(1, 0, 2);
            break;
        case S_GBR:
            assignPositions(1, 2, 0);
            break;
        case S_BRG:
            assignPositions(2, 0, 1);
            break;
        case S_BGR:
        default:
            assignPositions(2, 1, 0);
            break;
    }

    if (bytesPerPixel < 4) {
        fmt.format.w_pos = 3;
    }

    return fmt;
}

le_led_strip_encoder_timings_t timingsFromParams(const ledParams_t& params) {
    le_led_strip_encoder_timings_t timings = {};
    timings.t0h = params.T0H;
    timings.t1h = params.T1H;
    timings.t0l = params.T0L;
    timings.t1l = params.T1L;
    timings.reset = params.TRS / 1000;
    if (timings.reset == 0) {
        timings.reset = 50;
    }
    return timings;
}

le_led_model_t ledModelForType(const ledParams_t& params) {
    if (params.bytesPerPixel == 4) {
        return LE_LED_MODEL_SK6812;
    }
    return LE_LED_MODEL_WS2812;
}

uint8_t gamma8(uint8_t value) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * value) / 255);
}

constexpr uint32_t kLedStripRmtDefaultResolution = 10'000'000;
constexpr uint32_t kLedStripRmtQueueDepth = 4;
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
constexpr size_t kLedStripRmtDefaultMemSymbols = 64;
#else
constexpr size_t kLedStripRmtDefaultMemSymbols = 48;
#endif

struct rmt_led_strip_encoder_t {
    rmt_encoder_t base;
    rmt_encoder_t* bytes_encoder;
    rmt_encoder_t* copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
};

static size_t rmt_encode_led_strip(rmt_encoder_t* encoder, rmt_channel_handle_t channel, const void* primary_data,
                                   size_t data_size, rmt_encode_state_t* ret_state) {
    auto* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    uint32_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    switch (led_encoder->state) {
        case 0:
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = 1;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                break;
            }
            [[fallthrough]];
        case 1:
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                    sizeof(led_encoder->reset_code), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = 0;
                state |= RMT_ENCODING_COMPLETE;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                break;
            }
            break;
    }
    *ret_state = static_cast<rmt_encode_state_t>(state);
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t* encoder) {
    auto* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t* encoder) {
    auto* led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}

esp_err_t le_rmt_new_led_strip_encoder_with_timings(const le_led_strip_encoder_config_t* config,
                                                     rmt_encoder_handle_t* ret_encoder) {
    ESP_RETURN_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, kTag, "invalid encoder arguments");

    auto* led_encoder = static_cast<rmt_led_strip_encoder_t*>(calloc(1, sizeof(rmt_led_strip_encoder_t)));
    ESP_RETURN_ON_FALSE(led_encoder, ESP_ERR_NO_MEM, kTag, "no memory for encoder");

    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;

    auto to_ticks = [&](uint32_t duration_ns) -> uint32_t {
        return static_cast<uint32_t>((static_cast<uint64_t>(duration_ns) * config->resolution) / 1'000'000'000ULL);
    };

    rmt_bytes_encoder_config_t bytes_encoder_config = {};
    bytes_encoder_config.bit0.level0 = 1;
    bytes_encoder_config.bit0.duration0 = to_ticks(config->timings.t0h);
    bytes_encoder_config.bit0.level1 = 0;
    bytes_encoder_config.bit0.duration1 = to_ticks(config->timings.t0l);
    bytes_encoder_config.bit1.level0 = 1;
    bytes_encoder_config.bit1.duration0 = to_ticks(config->timings.t1h);
    bytes_encoder_config.bit1.level1 = 0;
    bytes_encoder_config.bit1.duration1 = to_ticks(config->timings.t1l);
    bytes_encoder_config.flags.msb_first = 1;

    esp_err_t err = rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder);
    if (err != ESP_OK) {
        free(led_encoder);
        return err;
    }

    rmt_copy_encoder_config_t copy_encoder_config = {};
    err = rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder);
    if (err != ESP_OK) {
        rmt_del_encoder(led_encoder->bytes_encoder);
        free(led_encoder);
        return err;
    }

    uint32_t reset_ticks = static_cast<uint32_t>((static_cast<uint64_t>(config->resolution) * config->timings.reset) / 1'000'000'000ULL);
    if (reset_ticks == 0) {
        reset_ticks = 1;
    }
    led_encoder->reset_code.level0 = 0;
    led_encoder->reset_code.duration0 = reset_ticks;
    led_encoder->reset_code.level1 = 0;
    led_encoder->reset_code.duration1 = reset_ticks;

    *ret_encoder = &led_encoder->base;
    return ESP_OK;
}

esp_err_t le_rmt_new_led_strip_encoder(const le_led_strip_encoder_config_t* config,
                                        rmt_encoder_handle_t* ret_encoder) {
    ESP_RETURN_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, kTag, "invalid encoder arguments");
    ESP_RETURN_ON_FALSE(config->led_model < LE_LED_MODEL_INVALID, ESP_ERR_INVALID_ARG, kTag, "invalid led model");

    le_led_strip_encoder_config_t timing_config = *config;
    bool has_timings = config->timings.t0h || config->timings.t0l || config->timings.t1h || config->timings.t1l ||
                       config->timings.reset;

    if (!has_timings) {
        if (config->led_model == LE_LED_MODEL_SK6812) {
            timing_config.timings = {300, 600, 900, 600, 280};
        } else if (config->led_model == LE_LED_MODEL_WS2812) {
            timing_config.timings = {300, 900, 900, 300, 280};
        } else if (config->led_model == LE_LED_MODEL_WS2811) {
            timing_config.timings = {500, 1200, 2000, 1300, 50};
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    }

    return le_rmt_new_led_strip_encoder_with_timings(&timing_config, ret_encoder);
}

esp_err_t le_led_strip_new_rmt_device(const le_led_strip_config_t* led_config,
                                       const le_led_strip_rmt_config_t* rmt_config,
                                       le_led_strip_handle_t* ret_strip) {
    LedStripRmtObj* rmt_strip = nullptr;
    esp_err_t ret = ESP_OK;
    le_led_color_component_format_t component_fmt = {};
    uint8_t mask = 0;
    uint8_t bytes_per_pixel = 0;
    uint32_t resolution = 0;
    rmt_clock_source_t clk_src = RMT_CLK_SRC_DEFAULT;
    size_t mem_block_symbols = 0;
    rmt_tx_channel_config_t rmt_chan_config = {};
    le_led_strip_encoder_config_t strip_encoder_conf = {};
    ESP_GOTO_ON_FALSE(led_config && rmt_config && ret_strip, ESP_ERR_INVALID_ARG, err, kTag, "invalid arguments");

    component_fmt = led_config->color_component_format;
    if (component_fmt.format_id == 0) {
        component_fmt = colorFormatForOrder(S_GRB, 3);
    }

    if (component_fmt.format.num_components == 3) {
        mask = BIT(component_fmt.format.r_pos) | BIT(component_fmt.format.g_pos) | BIT(component_fmt.format.b_pos);
        ESP_GOTO_ON_FALSE(mask == 0x07, ESP_ERR_INVALID_ARG, err, kTag, "invalid RGB order");
    } else if (component_fmt.format.num_components == 4) {
        mask = BIT(component_fmt.format.r_pos) | BIT(component_fmt.format.g_pos) |
               BIT(component_fmt.format.b_pos) | BIT(component_fmt.format.w_pos);
        ESP_GOTO_ON_FALSE(mask == 0x0F, ESP_ERR_INVALID_ARG, err, kTag, "invalid RGBW order");
    } else {
        ESP_GOTO_ON_FALSE(false, ESP_ERR_INVALID_ARG, err, kTag, "invalid color component count");
    }

    bytes_per_pixel = component_fmt.format.num_components ? component_fmt.format.num_components : 3;
    rmt_strip = static_cast<LedStripRmtObj*>(calloc(1, sizeof(LedStripRmtObj)));
    ESP_GOTO_ON_FALSE(rmt_strip, ESP_ERR_NO_MEM, err, kTag, "no memory for strip");

    if (led_config->external_pixel_buf != nullptr) {
        rmt_strip->pixel_buf = led_config->external_pixel_buf;
        rmt_strip->pixel_buf_allocated_internally = false;
    } else {
        size_t buf_size = led_config->max_leds * bytes_per_pixel;
        rmt_strip->pixel_buf = static_cast<uint8_t*>(calloc(buf_size, sizeof(uint8_t)));
        ESP_GOTO_ON_FALSE(rmt_strip->pixel_buf, ESP_ERR_NO_MEM, err, kTag, "no memory for pixels");
        rmt_strip->pixel_buf_allocated_internally = true;
    }

    resolution = rmt_config->resolution_hz ? rmt_config->resolution_hz : kLedStripRmtDefaultResolution;
    clk_src = rmt_config->clk_src ? rmt_config->clk_src : RMT_CLK_SRC_DEFAULT;
    mem_block_symbols = rmt_config->mem_block_symbols ? rmt_config->mem_block_symbols : kLedStripRmtDefaultMemSymbols;

    rmt_chan_config.clk_src = clk_src;
    rmt_chan_config.gpio_num = static_cast<gpio_num_t>(led_config->strip_gpio_num);
    rmt_chan_config.mem_block_symbols = mem_block_symbols;
    rmt_chan_config.resolution_hz = resolution;
    rmt_chan_config.trans_queue_depth = kLedStripRmtQueueDepth;
    rmt_chan_config.flags.with_dma = static_cast<uint32_t>(rmt_config->flags.with_dma);
    rmt_chan_config.flags.invert_out = static_cast<uint32_t>(led_config->flags.invert_out);
    ret = rmt_new_tx_channel(&rmt_chan_config, &rmt_strip->rmt_chan);
    ESP_GOTO_ON_ERROR(ret, err, kTag, "create RMT channel failed");

    strip_encoder_conf.resolution = resolution;
    strip_encoder_conf.led_model = led_config->led_model;
    strip_encoder_conf.timings = led_config->timings;
    ret = le_rmt_new_led_strip_encoder(&strip_encoder_conf, &rmt_strip->strip_encoder);
    ESP_GOTO_ON_ERROR(ret, err, kTag, "create encoder failed");

    rmt_strip->component_fmt = component_fmt;
    rmt_strip->bytes_per_pixel = bytes_per_pixel;
    rmt_strip->strip_len = led_config->max_leds;
    rmt_strip->tx_conf = {};
    rmt_strip->tx_conf.loop_count = 0;
    rmt_strip->base.set_pixel = led_strip_rmt_set_pixel;
    rmt_strip->base.set_pixel_rgbw = led_strip_rmt_set_pixel_rgbw;
    rmt_strip->base.refresh = led_strip_rmt_refresh;
    rmt_strip->base.clear = led_strip_rmt_clear;
    rmt_strip->base.del = led_strip_rmt_del;

    *ret_strip = &rmt_strip->base;
    return ESP_OK;

err:
    if (rmt_strip) {
        if (rmt_strip->rmt_chan) {
            rmt_del_channel(rmt_strip->rmt_chan);
        }
        if (rmt_strip->strip_encoder) {
            rmt_del_encoder(rmt_strip->strip_encoder);
        }
        if (rmt_strip->pixel_buf_allocated_internally && rmt_strip->pixel_buf) {
            free(rmt_strip->pixel_buf);
        }
        free(rmt_strip);
    }
    return ret;
}

} // namespace

namespace {

constexpr int kMaxStrands = 8;

strand_t g_strands[kMaxStrands];
uint8_t g_strandCount = 0;

const ledParams_t kLedParams[] = {
    {3, S_GRB, 350, 700, 800, 600, 50000},   // LED_WS2812_V1
    {3, S_GRB, 350, 900, 900, 350, 50000},   // LED_WS2812B_V1
    {3, S_GRB, 400, 850, 850, 400, 50000},   // LED_WS2812B_V2
    {3, S_GRB, 450, 850, 850, 450, 50000},   // LED_WS2812B_V3
    {3, S_GRB, 350, 800, 350, 350, 300000},  // LED_WS2813_V1
    {3, S_GRB, 270, 800, 800, 270, 300000},  // LED_WS2813_V2
    {3, S_GRB, 270, 630, 630, 270, 300000},  // LED_WS2813_V3
    {3, S_GRB, 220, 580, 580, 220, 300000},  // LED_WS2813_V4
    {3, S_RGB, 240, 750, 750, 240, 300100},  // LED_WS2815_V1
    {3, S_GRB, 300, 600, 900, 600, 80000},   // LED_SK6812_V1
    {4, S_GRB, 300, 600, 900, 600, 80000},   // LED_SK6812W_V1
    {4, S_GRB, 350, 700, 800, 600, 50000},   // LED_SK6812W_V3
    {4, S_GRB, 300, 600, 900, 600, 80000},   // LED_SK6812W_V4
    {3, S_GRB, 560, 480, 280, 640, 48000},   // LED_TM1934
};

struct DigitalLedsState {
    le_led_strip_handle_t stripHandle = nullptr;
    le_led_color_component_format_t colorFormat = {};
    uint8_t bytesPerPixel = 3;
    bool hasWhite = false;
};

} // namespace

int LibStrip::init() {
    return 0;
}

strand_t* LibStrip::addStrand(const strand_t& strand) {
    if (g_strandCount >= kMaxStrands) {
        ESP_LOGE(kTag, "Maximum strand count reached");
        return nullptr;
    }

    if (strand.ledType < 0 || strand.ledType >= static_cast<int>(sizeof(kLedParams) / sizeof(kLedParams[0]))) {
        ESP_LOGE(kTag, "Invalid LED type %d", strand.ledType);
        return nullptr;
    }

    const ledParams_t& params = kLedParams[strand.ledType];

    auto* pixels = static_cast<pixelColor_t*>(calloc(strand.numPixels, sizeof(pixelColor_t)));
    if (!pixels) {
        ESP_LOGE(kTag, "Pixel buffer allocation failed");
        return nullptr;
    }

    auto* state = new (std::nothrow) DigitalLedsState();
    if (!state) {
        free(pixels);
        ESP_LOGE(kTag, "State allocation failed");
        return nullptr;
    }

    const le_led_strip_encoder_timings_t timings = timingsFromParams(params);

    le_led_strip_config_t ledConfig = {};
    ledConfig.strip_gpio_num = strand.gpioNum;
    ledConfig.max_leds = strand.numPixels;
    ledConfig.led_model = ledModelForType(params);
    ledConfig.color_component_format = colorFormatForOrder(params.ledOrder, params.bytesPerPixel);
    ledConfig.external_pixel_buf = nullptr;
    ledConfig.flags.invert_out = 0;
    ledConfig.timings = timings;

    const bool isRgbw = params.bytesPerPixel == 4;
    le_led_strip_rmt_config_t rmtConfig = {};
    rmtConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    rmtConfig.resolution_hz = isRgbw ? 20000000 : 10000000;
    rmtConfig.mem_block_symbols = isRgbw ? 96 : 0;
    rmtConfig.flags.with_dma = 0;
    rmtConfig.interrupt_priority = 0;

    const double tickNs = 1'000'000'000.0 / static_cast<double>(rmtConfig.resolution_hz);
    ESP_LOGI(kTag,
             "LED type %d (%s) timings: T0H %.0fns (%u ticks) T0L %.0fns (%u ticks) T1H %.0fns (%u ticks) T1L %.0fns (%u ticks) reset %.0fns",
             strand.ledType,
             isRgbw ? "RGBW" : "RGB",
             static_cast<double>(timings.t0h), static_cast<uint32_t>(timings.t0h / tickNs + 0.5),
             static_cast<double>(timings.t0l), static_cast<uint32_t>(timings.t0l / tickNs + 0.5),
             static_cast<double>(timings.t1h), static_cast<uint32_t>(timings.t1h / tickNs + 0.5),
             static_cast<double>(timings.t1l), static_cast<uint32_t>(timings.t1l / tickNs + 0.5),
             static_cast<double>(timings.reset));

    le_led_strip_handle_t handle = nullptr;
    esp_err_t err = le_led_strip_new_rmt_device(&ledConfig, &rmtConfig, &handle);
    if (err != ESP_OK) {
        delete state;
        free(pixels);
        ESP_LOGE(kTag, "Failed to create RMT device: %d", err);
        return nullptr;
    }

    state->stripHandle = handle;
    state->colorFormat = ledConfig.color_component_format;
    state->bytesPerPixel = params.bytesPerPixel;
    state->hasWhite = (params.bytesPerPixel == 4);

    strand_t stored = strand;
    stored.pixels = pixels;
    stored._stateVars = state;

    g_strands[g_strandCount] = stored;
    return &g_strands[g_strandCount++];
}

int LibStrip::updatePixels(strand_t* strand) {
    if (!strand || !strand->_stateVars) {
        return -1;
    }

    auto* state = reinterpret_cast<DigitalLedsState*>(strand->_stateVars);
    if (!state->stripHandle || !strand->pixels) {
        return -1;
    }

    const int brightLimit = std::clamp(strand->brightLimit, 0, 255);

    for (int i = 0; i < strand->numPixels; ++i) {
        pixelColor_t color = strand->pixels[i];

        uint8_t r = gamma8(color.r);
        uint8_t g = gamma8(color.g);
        uint8_t b = gamma8(color.b);
        uint8_t w = gamma8(color.w);

        if (brightLimit == 0) {
            r = g = b = w = 0;
        } else if (brightLimit < 255) {
            r = static_cast<uint8_t>((static_cast<uint16_t>(r) * brightLimit) / 255);
            g = static_cast<uint8_t>((static_cast<uint16_t>(g) * brightLimit) / 255);
            b = static_cast<uint8_t>((static_cast<uint16_t>(b) * brightLimit) / 255);
            w = static_cast<uint8_t>((static_cast<uint16_t>(w) * brightLimit) / 255);
        }

        if (state->hasWhite) {
            le_led_strip_set_pixel_rgbw(state->stripHandle, i, r, g, b, w);
        } else {
            le_led_strip_set_pixel(state->stripHandle, i, r, g, b);
        }
    }

    return le_led_strip_refresh(state->stripHandle) == ESP_OK ? 0 : -1;
}

void LibStrip::resetStrand(strand_t* strand) {
    if (!strand || !strand->_stateVars) {
        return;
    }

    auto* state = reinterpret_cast<DigitalLedsState*>(strand->_stateVars);
    if (state->stripHandle) {
        le_led_strip_clear(state->stripHandle);
        le_led_strip_del(state->stripHandle);
        state->stripHandle = nullptr;
    }
    if (strand->pixels) {
        free(strand->pixels);
        strand->pixels = nullptr;
    }
    delete state;
    strand->_stateVars = nullptr;
}
 

