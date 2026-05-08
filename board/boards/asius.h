#pragma once

#include "board_declarations.h"
#ifndef BOOTSTUB
#include "board/obj/sound_data.h"
#endif

// ////////////////////////// //
// ASIUS (STM32H7) + Harness //
// ////////////////////////// //

// WS2812B on PB1 — bit-bang using DWT cycle counter at 240MHz CPU
#define WS2812B_PIN 1U
#define WS2812B_PORT GPIOB
#define WS2812B_SET (1U << WS2812B_PIN)
#define WS2812B_RST (1U << (WS2812B_PIN + 16U))

// WS2812B-2020 V1.3 timing (240MHz = 4.167ns/cycle)
#define WS2812B_T0H  72U   // 300ns  (spec: 220-380ns)
#define WS2812B_T0L  168U  // 700ns  (spec: 580-1000ns)
#define WS2812B_T1H  168U  // 700ns  (spec: 580-1000ns)
#define WS2812B_T1L  72U   // 300ns  (spec: 220-380ns)

static void ws2812b_init_dwt(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void ws2812b_send_byte(uint8_t byte) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    uint32_t t_high = ((byte >> bit) & 1U) ? WS2812B_T1H : WS2812B_T0H;
    uint32_t t_low = ((byte >> bit) & 1U) ? WS2812B_T1L : WS2812B_T0L;
    uint32_t start;

    WS2812B_PORT->BSRR = WS2812B_SET;
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < t_high) {}

    WS2812B_PORT->BSRR = WS2812B_RST;
    start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < t_low) {}
  }
}

static uint8_t ws2812b_rgb[3] = {0U, 0U, 0U};

static void ws2812b_update(void) {
  static bool dwt_ready = false;
  if (!dwt_ready) {
    ws2812b_init_dwt();
    dwt_ready = true;
  }
  __disable_irq();
  ws2812b_send_byte(ws2812b_rgb[1]); // G
  ws2812b_send_byte(ws2812b_rgb[0]); // R
  ws2812b_send_byte(ws2812b_rgb[2]); // B
  __enable_irq();
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < 72000U) {} // reset >300µs
}

#define WS2812B_BRIGHTNESS 32U
#define ASIUS_LED_START_R 0U
#define ASIUS_LED_START_G 36U
#define ASIUS_LED_START_B 180U
#define ASIUS_LED_STANDBY_R 0U
#define ASIUS_LED_STANDBY_G 6U
#define ASIUS_LED_STANDBY_B 32U
#define ASIUS_LED_ENGAGED_R 0U
#define ASIUS_LED_ENGAGED_G 180U
#define ASIUS_LED_ENGAGED_B 35U
#define ASIUS_LED_WARNING_R 180U
#define ASIUS_LED_WARNING_G 80U
#define ASIUS_LED_WARNING_B 0U
#define ASIUS_LED_CRITICAL_R 180U
#define ASIUS_LED_CRITICAL_G 0U
#define ASIUS_LED_CRITICAL_B 0U

static void asius__set_led(uint8_t color, bool enabled) {
  ws2812b_rgb[color] = enabled ? WS2812B_BRIGHTNESS : 0U;
  ws2812b_update();
}

static void asius__set_led_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  ws2812b_rgb[0] = red;
  ws2812b_rgb[1] = green;
  ws2812b_rgb[2] = blue;
  ws2812b_update();
}

#ifndef BOOTSTUB
static void asius__set_led_fallback(bool controls_allowed, bool power_save_enabled, uint8_t fault_status) {
  bool blink_on = (uptime_cnt & 1U) == 0U;

  if (fault_status == FAULT_STATUS_PERMANENT) {
    asius__set_led_rgb(blink_on ? ASIUS_LED_CRITICAL_R : 0U, 0U, 0U);
  } else if (fault_status == FAULT_STATUS_TEMPORARY) {
    asius__set_led_rgb(blink_on ? ASIUS_LED_WARNING_R : 0U, blink_on ? ASIUS_LED_WARNING_G : 0U, 0U);
  } else if (controls_allowed) {
    asius__set_led_rgb(ASIUS_LED_ENGAGED_R, ASIUS_LED_ENGAGED_G, ASIUS_LED_ENGAGED_B);
  } else if (blink_on && !power_save_enabled) {
    asius__set_led_rgb(ASIUS_LED_START_R, ASIUS_LED_START_G, ASIUS_LED_START_B);
  } else {
    asius__set_led_rgb(ASIUS_LED_STANDBY_R, ASIUS_LED_STANDBY_G, ASIUS_LED_STANDBY_B);
  }
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

static void asius__set_bootkick(BootState state) {
  set_gpio_output(GPIOA, 0, state != BOOT_BOOTKICK);
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

static void asius__mic_init(void) {
  set_gpio_alternate(GPIOD, 9, GPIO_AF3_DFSDM1);
  set_gpio_alternate(GPIOD, 10, GPIO_AF3_DFSDM1);

  register_set(&DFSDM1_Channel0->CHCFGR1, (90UL << DFSDM_CHCFGR1_CKOUTDIV_Pos) | DFSDM_CHCFGR1_CHEN, 0xC0FFF1EFU);
  register_set(&DFSDM1_Channel3->CHCFGR1, (0b01UL << DFSDM_CHCFGR1_SPICKSEL_Pos) | (0b00U << DFSDM_CHCFGR1_SITP_Pos) | DFSDM_CHCFGR1_CHEN, 0x0000F1EFU);
  register_set(&DFSDM1_Filter0->FLTFCR, (0U << DFSDM_FLTFCR_IOSR_Pos) | (54UL << DFSDM_FLTFCR_FOSR_Pos) | (4UL << DFSDM_FLTFCR_FORD_Pos), 0xE3FF00FFU);
  register_set(&DFSDM1_Filter0->FLTCR1, DFSDM_FLTCR1_FAST | (3UL << DFSDM_FLTCR1_RCH_Pos) | DFSDM_FLTCR1_RDMAEN | DFSDM_FLTCR1_RCONT | DFSDM_FLTCR1_DFEN, 0x672E7F3BU);

  register_set(&DMA1_Stream0->PAR, (uint32_t)&DFSDM1_Filter0->FLTRDATAR, 0xFFFFFFFFU);
  register_set(&DMA1_Stream0->M0AR, (uint32_t)mic_rx_buf[0], 0xFFFFFFFFU);
  register_set(&DMA1_Stream0->M1AR, (uint32_t)mic_rx_buf[1], 0xFFFFFFFFU);
  DMA1_Stream0->NDTR = 512U;
  register_set(&DMA1_Stream0->CR, DMA_SxCR_DBM | (0b10UL << DMA_SxCR_MSIZE_Pos) | (0b10UL << DMA_SxCR_PSIZE_Pos) | DMA_SxCR_MINC | DMA_SxCR_CIRC, 0x01F7FFFFU);
  register_set(&DMAMUX1_Channel0->CCR, 101U, DMAMUX_CxCR_DMAREQ_ID_Msk);
  register_set_bits(&DMA1_Stream0->CR, DMA_SxCR_EN);
  DMA1->LIFCR |= 0x7DU;

  register_set_bits(&DFSDM1_Channel0->CHCFGR1, DFSDM_CHCFGR1_DFSDMEN);
  DFSDM1_Filter0->FLTCR1 |= DFSDM_FLTCR1_RSWSTART;
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

  // IMU (LSM6DS3TR-C) on I2C5: PC10=SDA, PC11=SCL, PC9=INT1
  // TODO: I2C driver + read at 104Hz + transport over SPI/USB
  set_gpio_alternate(GPIOC, 10, GPIO_AF4_I2C5);  // I2C5_SDA
  set_gpio_alternate(GPIOC, 11, GPIO_AF4_I2C5);  // I2C5_SCL
  register_set_bits(&(GPIOC->OTYPER), GPIO_OTYPER_OT10 | GPIO_OTYPER_OT11);  // open drain for I2C
  set_gpio_mode(GPIOC, 9, MODE_INPUT);  // INT1 (EXTI)
  set_gpio_pullup(GPIOC, 9, PULL_NONE);  // INT1 active-high, no pull needed

#ifndef BOOTSTUB
  // PDM mic (PD9=DATIN3, PD10=CKOUT)
  asius__mic_init();
#endif
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
