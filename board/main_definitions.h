#include "main_declarations.h"

// ********************* Globals **********************
uint8_t hw_type = 0;
board *current_board;
uint32_t uptime_cnt = 0;

// heartbeat state
uint32_t heartbeat_counter = 0;
bool heartbeat_lost = false;
bool heartbeat_disabled = false;            // set over USB

// host LED override state
bool led_host_controlled = false;
uint32_t led_host_timeout = 0U;

// siren state
bool siren_enabled = false;
uint8_t siren_sound_id = 0U;
