#pragma once
#include <driver/i2s_std.h>

// Channel handle — used by melody.cpp to write samples
extern i2s_chan_handle_t i2s_tx_chan;

void i2sInit();
