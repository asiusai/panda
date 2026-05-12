#pragma once

#define WS2812_PIN 1U
#define WS2812_PORT GPIOB
#define WS2812_SET (1U << WS2812_PIN)
#define WS2812_RST (1U << (WS2812_PIN + 16U))

// DWT runs at 240 MHz. WS2812B wants ~1.25 us bits:
// 0: ~0.35 us high, ~0.90 us low; 1: ~0.70 us high, ~0.55 us low.
#define WS2812_T0H 84U
#define WS2812_T0L 216U
#define WS2812_T1H 168U
#define WS2812_T1L 132U
#define WS2812_RESET_CYCLES 72000U

#define WS2812_DEFAULT_R 0U
#define WS2812_DEFAULT_G 0U
#define WS2812_DEFAULT_B 255U

#ifndef BOOTSTUB
static bool ws2812_host_controlled = false;
static uint32_t ws2812_host_timeout = 0U;
static bool ws2812_default_bright = false;
#endif

static void ws2812_init_dwt(void) {
  static bool dwt_ready = false;
  if (!dwt_ready) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    dwt_ready = true;
  }
}

static void ws2812_send_byte(uint8_t byte) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    const bool one = ((byte >> bit) & 1U) != 0U;
    const uint32_t high_cycles = one ? WS2812_T1H : WS2812_T0H;
    const uint32_t low_cycles = one ? WS2812_T1L : WS2812_T0L;

    WS2812_PORT->BSRR = WS2812_SET;
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < high_cycles) {}

    WS2812_PORT->BSRR = WS2812_RST;
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < low_cycles) {}
  }
}

static void ws2812_write_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  if (hw_type != HW_TYPE_ASIUS) {
    return;
  }

  ws2812_init_dwt();

  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < WS2812_RESET_CYCLES) {}

  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  ws2812_send_byte(green);
  ws2812_send_byte(red);
  ws2812_send_byte(blue);
  if (primask == 0U) {
    __enable_irq();
  }
}

static void ws2812_init(void) {
  if (hw_type != HW_TYPE_ASIUS) {
    return;
  }

  set_gpio_pullup(WS2812_PORT, WS2812_PIN, PULL_NONE);
  set_gpio_output_type(WS2812_PORT, WS2812_PIN, OUTPUT_TYPE_PUSH_PULL);
  set_gpio_mode(WS2812_PORT, WS2812_PIN, MODE_OUTPUT);
  set_gpio_output(WS2812_PORT, WS2812_PIN, false);
  ws2812_write_rgb(0U, 0U, 0U);
}

#ifndef BOOTSTUB
static void ws2812_set_rgb565(uint16_t rgb565, uint16_t timeout_seconds) {
  if (hw_type != HW_TYPE_ASIUS) {
    return;
  }

  if (timeout_seconds == 0U) {
    ws2812_host_controlled = false;
    ws2812_write_rgb(0U, 0U, 0U);
  } else {
    const uint8_t red = (uint8_t)((((rgb565 >> 11) & 0x1FU) * 255U) / 31U);
    const uint8_t green = (uint8_t)((((rgb565 >> 5) & 0x3FU) * 255U) / 63U);
    const uint8_t blue = (uint8_t)(((rgb565 & 0x1FU) * 255U) / 31U);
    ws2812_host_controlled = true;
    ws2812_host_timeout = (timeout_seconds == 0xFFFFU) ? UINT32_MAX : (uptime_cnt + (uint32_t)timeout_seconds * 8U);
    ws2812_write_rgb(red, green, blue);
  }
}

static void ws2812_tick(void) {
  if (hw_type != HW_TYPE_ASIUS) {
    return;
  }

  if (ws2812_host_controlled && (ws2812_host_timeout != UINT32_MAX) && (uptime_cnt >= ws2812_host_timeout)) {
    ws2812_host_controlled = false;
    ws2812_write_rgb(0U, 0U, 0U);
  }

  if (!ws2812_host_controlled) {
    ws2812_default_bright = !ws2812_default_bright;
    ws2812_write_rgb(WS2812_DEFAULT_R, WS2812_DEFAULT_G, ws2812_default_bright ? WS2812_DEFAULT_B : 0U);
  }
}
#else
__attribute__((unused)) static void ws2812_set_rgb565(uint16_t rgb565, uint16_t timeout_seconds) {
  UNUSED(rgb565);
  UNUSED(timeout_seconds);
}

__attribute__((unused)) static void ws2812_tick(void) {
}
#endif
