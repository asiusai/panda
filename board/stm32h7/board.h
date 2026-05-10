// ///////////////////////////////////////////////////////////// //
// Hardware abstraction layer for all different supported boards //
// ///////////////////////////////////////////////////////////// //
#include "board/boards/board_declarations.h"
#include "board/boards/unused_funcs.h"

// ///// Board definition and detection ///// //
#include "board/stm32h7/lladc.h"
#include "board/drivers/harness.h"
#include "board/drivers/fan.h"
#include "board/stm32h7/llfan.h"
#include "board/stm32h7/sound.h"
#include "board/drivers/fake_siren.h"
#include "board/drivers/clock_source.h"
#include "board/boards/red.h"
#include "board/boards/tres.h"
#include "board/boards/cuatro.h"
#include "board/boards/asius.h"


void detect_board_type(void) {
  hw_type = HW_TYPE_ASIUS;
  current_board = &board_asius;

  // WS2812B on PB1: configure early so led_init works.
  set_gpio_pullup(GPIOB, 1, PULL_NONE);
  set_gpio_mode(GPIOB, 1, MODE_OUTPUT);
  set_gpio_output_type(GPIOB, 1, OUTPUT_TYPE_PUSH_PULL);
  register_set_bits(&(GPIOB->OSPEEDR), GPIO_OSPEEDR_OSPEED1);
  set_gpio_output(GPIOB, 1, 0);
}
