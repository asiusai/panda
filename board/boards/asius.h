#pragma once

#include "board_declarations.h"

// ////////////////////////// //
// ASIUS (STM32H7) + Harness //
// ////////////////////////// //

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

static uint32_t asius__read_current_mA(void) {
  return adc_get_mV(&(const adc_signal_t) ADC_CHANNEL_DEFAULT(ADC1, 3)) * 2U;
}

static void asius__set_fan_enabled(bool enabled) {
  set_gpio_output(GPIOD, 3, !enabled);
}

static void asius__set_bootkick(BootState state) {
  set_gpio_output(GPIOA, 0, state != BOOT_BOOTKICK);
  // DC_IN rising edge wakes SOM from ship mode
  set_gpio_output(GPIOC, 11, state != BOOT_BOOTKICK);
}

static void asius__set_amp_enabled(bool enabled) {
  set_gpio_output(GPIOB, 0, enabled);
}

static void asius__init(void) {
  common_init_gpio();

  // open drain
  set_gpio_output_type(GPIOD, 3, OUTPUT_TYPE_OPEN_DRAIN); // FAN_EN
  set_gpio_output_type(GPIOC, 11, OUTPUT_TYPE_OPEN_DRAIN); // DC_IN_EN_N

  // Power readout
  set_gpio_mode(GPIOC, 5, MODE_ANALOG);
  set_gpio_mode(GPIOA, 6, MODE_ANALOG);

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

  // C2: SOM GPIO used as input (fan control at boot)
  set_gpio_mode(GPIOC, 2, MODE_INPUT);
  set_gpio_pullup(GPIOC, 2, PULL_DOWN);

  // SOM bootkick + reset lines
  asius__set_bootkick(BOOT_BOOTKICK);

  // SOM debugging UART
  gpio_uart7_init();
  uart_init(&uart_ring_som_debug, 115200);

  // fan setup
  set_gpio_alternate(GPIOC, 8, GPIO_AF2_TIM3);
  register_set_bits(&(GPIOC->OTYPER), GPIO_OTYPER_OT8); // open drain

  // Clock source
  clock_source_init(true);

  // Sound codec
  asius__set_amp_enabled(false);
  set_gpio_alternate(GPIOA, 2, GPIO_AF8_SAI4);    // SAI4_SCK_B
  set_gpio_alternate(GPIOC, 0, GPIO_AF8_SAI4);    // SAI4_FS_B
  set_gpio_alternate(GPIOD, 11, GPIO_AF10_SAI4);  // SAI4_SD_A
  set_gpio_alternate(GPIOE, 3, GPIO_AF8_SAI4);    // SAI4_SD_B
  set_gpio_alternate(GPIOE, 4, GPIO_AF3_DFSDM1);  // DFSDM1_DATIN3
  set_gpio_alternate(GPIOE, 9, GPIO_AF3_DFSDM1);  // DFSDM1_CKOUT
  set_gpio_alternate(GPIOE, 6, GPIO_AF10_SAI4);   // SAI4_MCLK_B
  sound_init();
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
  .has_fan = true,
  .avdd_mV = 1800U,
  .fan_enable_cooldown_time = 3U,
  .init = asius__init,
  .init_bootloader = unused_init_bootloader,
  .enable_can_transceiver = asius__enable_can_transceiver,
  .led_GPIO = {GPIOC, GPIOC, GPIOC},
  .led_pin = {6, 7, 9},
  .led_pwm_channels = {1, 2, 4},
  .set_can_mode = asius_set_can_mode,
  .read_voltage_mV = asius__read_voltage_mV,
  .read_current_mA = asius__read_current_mA,
  .set_fan_enabled = asius__set_fan_enabled,
  .set_ir_power = unused_set_ir_power,
  .set_siren = fake_siren_set,
  .set_bootkick = asius__set_bootkick,
  .read_som_gpio = asius_read_som_gpio,
  .set_amp_enabled = asius__set_amp_enabled
};
