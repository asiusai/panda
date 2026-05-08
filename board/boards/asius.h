#pragma once

#include "board_declarations.h"
#ifndef BOOTSTUB
#include "board/obj/sound_data.h"
#endif

// ////////////////////////// //
// ASIUS (STM32H7) + Harness //
// ////////////////////////// //

// WS2812B on PB1. Keep updates rare: the 24 data bits need a short critical
// section, but the reset latch can wait with interrupts enabled.
#define WS2812B_PIN 1U
#define WS2812B_PORT GPIOB
#define WS2812B_SET (1U << WS2812B_PIN)
#define WS2812B_RST (1U << (WS2812B_PIN + 16U))
#define WS2812B_T0H  72U
#define WS2812B_T0L  168U
#define WS2812B_T1H  168U
#define WS2812B_T1L  72U
#define WS2812B_RESET_CYCLES 72000U
#define WS2812B_BRIGHTNESS 32U

#define ASIUS_LED_BLUE_R 0U
#define ASIUS_LED_BLUE_G 36U
#define ASIUS_LED_BLUE_B 180U

static void ws2812b_init_dwt(void) {
  static bool dwt_ready = false;
  if (!dwt_ready) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    dwt_ready = true;
  }
}

static void ws2812b_send_byte(uint8_t byte) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    const bool one = ((byte >> bit) & 1U) != 0U;
    const uint32_t t_high = one ? WS2812B_T1H : WS2812B_T0H;
    const uint32_t t_low = one ? WS2812B_T1L : WS2812B_T0L;

    WS2812B_PORT->BSRR = WS2812B_SET;
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < t_high) {}

    WS2812B_PORT->BSRR = WS2812B_RST;
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < t_low) {}
  }
}

static void ws2812b_write_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  ws2812b_init_dwt();

  static uint8_t last_red = 0xFFU;
  static uint8_t last_green = 0xFFU;
  static uint8_t last_blue = 0xFFU;
  if ((red == last_red) && (green == last_green) && (blue == last_blue)) {
    return;
  }

  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < WS2812B_RESET_CYCLES) {}

  __disable_irq();
  ws2812b_send_byte(green);
  ws2812b_send_byte(red);
  ws2812b_send_byte(blue);
  __enable_irq();

  last_red = red;
  last_green = green;
  last_blue = blue;
}

static void asius__set_led(uint8_t color, bool enabled) {
  static uint8_t rgb[3] = {0U, 0U, 0U};
  if (color < 3U) {
    rgb[color] = enabled ? WS2812B_BRIGHTNESS : 0U;
    ws2812b_write_rgb(rgb[0], rgb[1], rgb[2]);
  }
}

static void asius__set_led_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  ws2812b_write_rgb(red, green, blue);
}

#ifndef BOOTSTUB
static void asius__set_led_fallback(bool controls_allowed, bool power_save_enabled, uint8_t fault_status) {
  UNUSED(controls_allowed);
  UNUSED(power_save_enabled);
  UNUSED(fault_status);

  asius__set_led_rgb(ASIUS_LED_BLUE_R, ASIUS_LED_BLUE_G, ASIUS_LED_BLUE_B);
}
#endif

static void asius__enable_can_transceiver(uint8_t transceiver, bool enabled) {
  switch (transceiver) {
    case 1U:
      set_gpio_output(GPIOB, 7, !enabled);
      break;
    case 2U:
      set_gpio_output(GPIOB, 10, !enabled);
      break;
    case 3U:
      set_gpio_output(GPIOD, 8, !enabled);
      break;
    case 4U:
      set_gpio_output(GPIOB, 11, !enabled);
      break;
    default:
      break;
  }
}

static uint32_t asius__read_voltage_mV(void) {
  return adc_get_mV(&(const adc_signal_t) ADC_CHANNEL_DEFAULT(ADC1, 8)) * 11U;
}

// TODO: make bootkick work, rn it just restarted the board every x seconds
static void asius__set_bootkick(BootState state) {
  UNUSED(state);
  set_gpio_output(GPIOA, 0, true);
}

static void asius__set_amp_enabled(bool enabled) {
  set_gpio_output(GPIOD, 7, enabled);
}

#ifndef BOOTSTUB
// DMA1_Stream1 NDTR is 16-bit (max 65535). Sounds longer than that need chaining via TC interrupt.
static const uint16_t *siren_dma_next;
static uint32_t siren_dma_remaining;
static volatile bool siren_dma_done;

#define SIREN_DMA_MAX 65535U

static void siren_dma_load_chunk(void) {
  register_clear_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
  while ((DMA1_Stream1->CR & DMA_SxCR_EN) != 0U) {}
  DMA1->LIFCR = (0x3FU << 6);
  register_set(&DMA1_Stream1->M0AR, (uint32_t)siren_dma_next, 0xFFFFFFFFU);
  uint32_t chunk = (siren_dma_remaining > SIREN_DMA_MAX) ? SIREN_DMA_MAX : siren_dma_remaining;
  DMA1_Stream1->NDTR = (uint16_t)chunk;
  siren_dma_next += chunk;
  siren_dma_remaining -= chunk;
  register_set_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
}

void DMA1_Stream1_IRQ_Handler(void) {
  if ((DMA1->LISR & DMA_LISR_TCIF1) != 0U) {
    DMA1->LIFCR = DMA_LIFCR_CTCIF1;
    if (siren_dma_remaining > 0U) {
      siren_dma_load_chunk();
      DAC1->SR = DAC_SR_DMAUDR1;
    } else {
      siren_dma_done = true;
    }
  }
  DMA1->LIFCR = (0x3FU << 6);
}

static void asius__siren_tim_init(void) {
  register_set(&TIM7->PSC, 0U, 0xFFFFU);
  register_set(&TIM7->ARR, 2499U, 0xFFFFU);
  register_set(&TIM7->CR2, (0b10U << TIM_CR2_MMS_Pos), TIM_CR2_MMS_Msk);
  register_set(&TIM7->CR1, TIM_CR1_ARPE | TIM_CR1_URS, 0x088EU);
  TIM7->SR = 0U;
}

static void asius__siren_dac_init(void) {
  // HFSEL=01 for AHB 80-160MHz (ours is 120MHz), MODE=000 (external pin, buffer enabled)
  register_set(&DAC1->MCR, (0b01U << 8), 0xFFFFFFFFU);
  DAC1->DHR12R1 = 2048U;
  register_set(&DAC1->CR, DAC_CR_TEN1 | (6U << DAC_CR_TSEL1_Pos) | DAC_CR_DMAEN1, 0xFFFFFFFFU);
  register_set_bits(&DAC1->CR, DAC_CR_EN1);
  // Clear any DMA underrun flag
  DAC1->SR = DAC_SR_DMAUDR1;
}

static void asius__siren_dma_init(const uint16_t *data, uint32_t len) {
  register_set(&DMAMUX1_Channel1->CCR, 67U, DMAMUX_CxCR_DMAREQ_ID_Msk);
  register_set(&DMA1_Stream1->PAR, (uint32_t)&(DAC1->DHR12R1), 0xFFFFFFFFU);
  register_set(&DMA1_Stream1->FCR, 0U, 0x00000083U);
  siren_dma_next = data;
  siren_dma_remaining = len;
  siren_dma_done = false;
  DMA1_Stream1->CR = (0b11UL << DMA_SxCR_PL_Pos) | (0b01UL << DMA_SxCR_MSIZE_Pos) | (0b01UL << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | (1U << DMA_SxCR_DIR_Pos) | DMA_SxCR_TCIE;
  siren_dma_load_chunk();
}

// called at 8Hz from tick_handler
static void asius__siren_set(bool enabled) {
  static bool initialized = false;
  static bool started = false;
  static bool played = false;
  static uint8_t last_sound_id = 0U;

  if (!initialized) {
    asius__siren_tim_init();
    REGISTER_INTERRUPT(DMA1_Stream1_IRQn, DMA1_Stream1_IRQ_Handler, 128U, FAULT_INTERRUPT_RATE_SOUND_DMA)
    NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    initialized = true;
  }

  if (enabled && siren_sound_id > 0U && siren_sound_id < OP_SOUNDS_COUNT) {
    if (siren_sound_id != last_sound_id) {
      played = false;
      last_sound_id = siren_sound_id;
    }
    if (!started && !played) {
      register_clear_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
      while ((DMA1_Stream1->CR & DMA_SxCR_EN) != 0U) {}
      DMA1->LIFCR = (0x3FU << 6);
      // DAC + DMA ready before TIM7 starts, to avoid DMAUDR1
      asius__siren_dac_init();
      asius__siren_dma_init(op_sounds[siren_sound_id].data, op_sounds[siren_sound_id].len);
      current_board->set_amp_enabled(true);
      TIM7->CNT = 0U;
      DAC1->SR = DAC_SR_DMAUDR1;
      TIM7->CR1 |= TIM_CR1_CEN;
      started = true;
    }
    if (started && siren_dma_done) {
      current_board->set_amp_enabled(false);
      register_clear_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
      DMA1->LIFCR = (0x3FU << 6);
      register_clear_bits(&DAC1->CR, DAC_CR_DMAEN1 | DAC_CR_TEN1 | DAC_CR_EN1);
      TIM7->CR1 &= ~TIM_CR1_CEN;
      started = false;
      played = true;
    }
  } else {
    if (started) {
      current_board->set_amp_enabled(false);
      register_clear_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
      DMA1->LIFCR = (0x3FU << 6);
      register_clear_bits(&DAC1->CR, DAC_CR_DMAEN1 | DAC_CR_TEN1 | DAC_CR_EN1);
      TIM7->CR1 &= ~TIM_CR1_CEN;
      started = false;
    }
    played = false;
  }
}

#endif

static void asius__init(void) {
  common_init_gpio();

  // WS2812B LED on PB1 (push-pull, not open drain)
  set_gpio_pullup(GPIOB, 1, PULL_NONE);
  set_gpio_mode(GPIOB, 1, MODE_OUTPUT);
  set_gpio_output(GPIOB, 1, 0);

  // Power readout
  set_gpio_mode(GPIOC, 5, MODE_ANALOG);

  // CAN transceiver enables
  set_gpio_pullup(GPIOB, 7, PULL_NONE);
  set_gpio_mode(GPIOB, 7, MODE_OUTPUT);
  set_gpio_pullup(GPIOD, 8, PULL_NONE);
  set_gpio_mode(GPIOD, 8, MODE_OUTPUT);

  // FDCAN3, different pins on this package than the rest of the reds
  set_gpio_pullup(GPIOD, 12, PULL_NONE);
  set_gpio_alternate(GPIOD, 12, GPIO_AF5_FDCAN3);
  set_gpio_pullup(GPIOD, 13, PULL_NONE);
  set_gpio_alternate(GPIOD, 13, GPIO_AF5_FDCAN3);

  // C2: SOM GPIO used as input
  set_gpio_mode(GPIOC, 2, MODE_INPUT);
  set_gpio_pullup(GPIOC, 2, PULL_DOWN);

  // SOM bootkick + reset lines
  asius__set_bootkick(BOOT_BOOTKICK);

  // Clock source
  clock_source_init(true);

  asius__set_amp_enabled(false);

}


static void asius_set_can_mode(uint8_t mode) {
  current_board->enable_can_transceiver(2U, false);
  current_board->enable_can_transceiver(4U, false);
  switch (mode) {
    case CAN_MODE_NORMAL:
    case CAN_MODE_OBD_CAN2:
      if ((bool)(mode == CAN_MODE_NORMAL) != (bool)(harness.status == HARNESS_STATUS_FLIPPED)) {
        // B12,B13: disable normal mode
        set_gpio_pullup(GPIOB, 12, PULL_NONE);
        set_gpio_mode(GPIOB, 12, MODE_ANALOG);

        set_gpio_pullup(GPIOB, 13, PULL_NONE);
        set_gpio_mode(GPIOB, 13, MODE_ANALOG);

        // B5,B6: FDCAN2 mode
        set_gpio_pullup(GPIOB, 5, PULL_NONE);
        set_gpio_alternate(GPIOB, 5, GPIO_AF9_FDCAN2);

        set_gpio_pullup(GPIOB, 6, PULL_NONE);
        set_gpio_alternate(GPIOB, 6, GPIO_AF9_FDCAN2);
        current_board->enable_can_transceiver(2U, true);
      } else {
        // B5,B6: disable normal mode
        set_gpio_pullup(GPIOB, 5, PULL_NONE);
        set_gpio_mode(GPIOB, 5, MODE_ANALOG);

        set_gpio_pullup(GPIOB, 6, PULL_NONE);
        set_gpio_mode(GPIOB, 6, MODE_ANALOG);
        // B12,B13: FDCAN2 mode
        set_gpio_pullup(GPIOB, 12, PULL_NONE);
        set_gpio_alternate(GPIOB, 12, GPIO_AF9_FDCAN2);

        set_gpio_pullup(GPIOB, 13, PULL_NONE);
        set_gpio_alternate(GPIOB, 13, GPIO_AF9_FDCAN2);
        current_board->enable_can_transceiver(4U, true);
      }
      break;
    default:
      break;
  }
}

static bool asius_read_som_gpio (void) {
  return (get_gpio_input(GPIOC, 2) != 0);
}

static harness_configuration asius__harness_config = {
  .GPIO_SBU1 = GPIOC,
  .GPIO_SBU2 = GPIOA,
  .GPIO_relay_SBU1 = GPIOA,
  .GPIO_relay_SBU2 = GPIOA,
  .pin_SBU1 = 4,
  .pin_SBU2 = 1,
  .pin_relay_SBU1 = 9,
  .pin_relay_SBU2 = 3,
  .adc_signal_SBU1 = ADC_CHANNEL_DEFAULT(ADC1, 4),
  .adc_signal_SBU2 = ADC_CHANNEL_DEFAULT(ADC1, 17)
};

board board_asius = {
  .harness_config = &asius__harness_config,
  .has_spi = true,
  .has_fan = false,
  .avdd_mV = 1800U,
  .fan_enable_cooldown_time = 0U,
  .init = asius__init,
  .init_bootloader = unused_init_bootloader,
  .enable_can_transceiver = asius__enable_can_transceiver,
  .led_GPIO = {GPIOB, GPIOB, GPIOB},
  .led_pin = {1, 1, 1},
  .led_pwm_channels = {0, 0, 0},
  .set_can_mode = asius_set_can_mode,
  .read_voltage_mV = asius__read_voltage_mV,
  .read_current_mA = unused_read_current,
  .set_fan_enabled = unused_set_fan_enabled,
  .set_ir_power = unused_set_ir_power,
#ifndef BOOTSTUB
  .set_siren = asius__siren_set,
#else
  .set_siren = unused_set_siren,
#endif
  .set_bootkick = asius__set_bootkick,
  .read_som_gpio = asius_read_som_gpio,
  .set_amp_enabled = asius__set_amp_enabled,
  .set_led = asius__set_led,
  .set_led_rgb = asius__set_led_rgb,
#ifndef BOOTSTUB
  .set_led_fallback = asius__set_led_fallback
#endif
};
