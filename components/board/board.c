#include "board.h"
#include "board_definition.h"
const board_config_t *board_get_config(void) { return &board_definition; }
