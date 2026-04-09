#pragma once

#include "board_declarations.h"
#include "board/drivers/sound_data.h"

// ////////////////////////// //
// ASIUS (STM32H7) + Harness //
// ////////////////////////// //

// WS2812B on PB1 — bit-bang at 240MHz CPU
#define WS2812B_PIN 1U
#define WS2812B_PORT GPIOB
#define WS2812B_SET (1U << WS2812B_PIN)
#define WS2812B_RST (1U << (WS2812B_PIN + 16U))

// Delay loop calibrated for 240MHz: ~4 cycles per iteration
static void ws2812b_delay(uint32_t n) {
  for (volatile uint32_t i = 0U; i < n; i++) {}
}

static void ws2812b_send_byte(uint8_t byte) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    if ((byte >> bit) & 1U) {
      // T1H ~0.8µs, T1L ~0.45µs
      WS2812B_PORT->BSRR = WS2812B_SET;
      ws2812b_delay(38U);
      WS2812B_PORT->BSRR = WS2812B_RST;
      ws2812b_delay(18U);
    } else {
      // T0H ~0.4µs, T0L ~0.85µs
      WS2812B_PORT->BSRR = WS2812B_SET;
      ws2812b_delay(16U);
      WS2812B_PORT->BSRR = WS2812B_RST;
      ws2812b_delay(40U);
    }
  }
}

static uint8_t ws2812b_rgb[3] = {0U, 0U, 0U}; // R, G, B state

static void ws2812b_update(void) {
  __disable_irq();
  // WS2812B expects GRB order
  ws2812b_send_byte(ws2812b_rgb[1]); // G
  ws2812b_send_byte(ws2812b_rgb[0]); // R
  ws2812b_send_byte(ws2812b_rgb[2]); // B
  __enable_irq();
  // Reset: >50µs low (pin already low after last bit)
  ws2812b_delay(500U);
}

// LED brightness (WS2812B is bright, keep it low)
#define WS2812B_BRIGHTNESS 8U

static void asius__set_led(uint8_t color, bool enabled) {
  ws2812b_rgb[color] = enabled ? WS2812B_BRIGHTNESS : 0U;
  ws2812b_update();
}

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

static void asius__set_bootkick(BootState state) {
  set_gpio_output(GPIOA, 0, state != BOOT_BOOTKICK);
}

static void asius__set_amp_enabled(bool enabled) {
  set_gpio_output(GPIOD, 7, enabled);
}

static void asius__siren_tim_init(void) {
  // 48kHz sample rate: 120MHz / (2499+1) = 48kHz
  register_set(&TIM7->PSC, 0U, 0xFFFFU);
  register_set(&TIM7->ARR, 2499U, 0xFFFFU);
  register_set(&TIM7->CR2, (0b10U << TIM_CR2_MMS_Pos), TIM_CR2_MMS_Msk);
  register_set(&TIM7->CR1, TIM_CR1_ARPE | TIM_CR1_URS, 0x088EU);
  TIM7->SR = 0U;
  TIM7->CR1 |= TIM_CR1_CEN;
}

static void asius__siren_dac_init(void) {
  DAC1->DHR12R1 = 2048U;
  register_set(&DAC1->MCR, 0U, 0xFFFFFFFFU);
  register_set(&DAC1->CR, DAC_CR_TEN1 | (6U << DAC_CR_TSEL1_Pos) | DAC_CR_DMAEN1, 0xFFFFFFFFU);
  register_set_bits(&DAC1->CR, DAC_CR_EN1);
}

static void asius__siren_dma_init(const uint16_t *data, uint32_t len) {
  register_set(&DMAMUX1_Channel1->CCR, 67U, DMAMUX_CxCR_DMAREQ_ID_Msk);
  register_set(&DMA1_Stream1->PAR, (uint32_t)&(DAC1->DHR12R1), 0xFFFFFFFFU);
  register_set(&DMA1_Stream1->M0AR, (uint32_t)data, 0xFFFFFFFFU);
  register_set(&DMA1_Stream1->FCR, 0U, 0x00000083U);
  DMA1_Stream1->NDTR = len;
  // 16-bit memory, 16-bit peripheral, one-shot
  DMA1_Stream1->CR = (0b11UL << DMA_SxCR_PL_Pos) | (0b01UL << DMA_SxCR_MSIZE_Pos) | (0b01UL << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | (1U << DMA_SxCR_DIR_Pos);
}

static void asius__siren_set(bool enabled) {
  static bool initialized = false;
  static bool started = false;

  if (!initialized) {
    asius__siren_tim_init();
    initialized = true;
  }

  if (enabled && siren_sound_id > 0U && siren_sound_id < OP_SOUNDS_COUNT) {
    if (!started) {
      register_clear_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
      while ((DMA1_Stream1->CR & DMA_SxCR_EN) != 0U) {}
      DMA1->LIFCR = (0x3FU << 6);
      asius__siren_dac_init();
      asius__siren_dma_init(op_sounds[siren_sound_id].data, op_sounds[siren_sound_id].len);
      current_board->set_amp_enabled(true);
      register_set_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
      started = true;
    }
  } else {
    if (started) {
      current_board->set_amp_enabled(false);
      register_clear_bits(&DMA1_Stream1->CR, DMA_SxCR_EN);
      DMA1->LIFCR = (0x3FU << 6);
      DAC1->DHR12R1 = 2048U;
      started = false;
    }
  }
}

// PDM mic capture via DFSDM1 (PD9=DATIN3, PD10=CKOUT)
// Same channels as cuatro — no DFSDM register changes needed
// TODO: transport mic data to SOM over SPI/USB
__attribute__((section(".sram4"))) static uint32_t asius_mic_rx_buf[2][512];

static void asius__mic_init(void) {
  // GPIO setup
  set_gpio_alternate(GPIOD, 9, GPIO_AF3_DFSDM1);  // DFSDM1_DATIN3
  set_gpio_alternate(GPIOD, 10, GPIO_AF3_DFSDM1);  // DFSDM1_CKOUT

  // DFSDM clock output on channel 0, mic data on channel 3
  register_set(&DFSDM1_Channel0->CHCFGR1, (90UL << DFSDM_CHCFGR1_CKOUTDIV_Pos) | DFSDM_CHCFGR1_CHEN, 0xC0FFF1EFU);
  register_set(&DFSDM1_Channel3->CHCFGR1, (0b01UL << DFSDM_CHCFGR1_SPICKSEL_Pos) | (0b00U << DFSDM_CHCFGR1_SITP_Pos) | DFSDM_CHCFGR1_CHEN, 0x0000F1EFU);
  register_set(&DFSDM1_Filter0->FLTFCR, (0U << DFSDM_FLTFCR_IOSR_Pos) | (54UL << DFSDM_FLTFCR_FOSR_Pos) | (4UL << DFSDM_FLTFCR_FORD_Pos), 0xE3FF00FFU);
  register_set(&DFSDM1_Filter0->FLTCR1, DFSDM_FLTCR1_FAST | (3UL << DFSDM_FLTCR1_RCH_Pos) | DFSDM_FLTCR1_RDMAEN | DFSDM_FLTCR1_RCONT | DFSDM_FLTCR1_DFEN, 0x672E7F3BU);

  // DMA (DFSDM1 -> memory, double buffer, circular)
  register_set(&DMA1_Stream0->PAR, (uint32_t)&DFSDM1_Filter0->FLTRDATAR, 0xFFFFFFFFU);
  register_set(&DMA1_Stream0->M0AR, (uint32_t)asius_mic_rx_buf[0], 0xFFFFFFFFU);
  register_set(&DMA1_Stream0->M1AR, (uint32_t)asius_mic_rx_buf[1], 0xFFFFFFFFU);
  DMA1_Stream0->NDTR = 512U;
  register_set(&DMA1_Stream0->CR, DMA_SxCR_DBM | (0b10UL << DMA_SxCR_MSIZE_Pos) | (0b10UL << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | DMA_SxCR_CIRC, 0x01F7FFFFU);
  register_set(&DMAMUX1_Channel0->CCR, 101U, DMAMUX_CxCR_DMAREQ_ID_Msk); // DFSDM1_DMA0
  register_set_bits(&DMA1_Stream0->CR, DMA_SxCR_EN);
  DMA1->LIFCR |= 0x7DU;

  // Start DFSDM conversion
  register_set_bits(&DFSDM1_Channel0->CHCFGR1, DFSDM_CHCFGR1_DFSDMEN);
  DFSDM1_Filter0->FLTCR1 |= DFSDM_FLTCR1_RSWSTART;
}

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

  // Amp off by default (PAM8302A on PD7, audio input on PA4)
  asius__set_amp_enabled(false);

  // PDM mic (PD9=DATIN3, PD10=CKOUT)
  asius__mic_init();
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

board board_asius_ = {
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
  .set_siren = asius__siren_set,
  .set_bootkick = asius__set_bootkick,
  .read_som_gpio = asius_read_som_gpio,
  .set_amp_enabled = asius__set_amp_enabled,
  .set_led = asius__set_led
};
