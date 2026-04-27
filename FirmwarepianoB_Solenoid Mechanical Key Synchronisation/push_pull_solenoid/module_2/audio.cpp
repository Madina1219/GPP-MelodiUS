#include "audio.h"
#include "config.h"
#include <driver/i2s_std.h>
#include <Arduino.h>

// Handle exposed via audio.h so melody.cpp can write samples
i2s_chan_handle_t i2s_tx_chan = NULL;

void i2sInit() {
  // 1. Create TX channel — auto_clear means DMA outputs 0s on
  //    underrun instead of replaying stale data (kills the buzz)
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num    = 8;
  chan_cfg.dma_frame_num   = 64;
  chan_cfg.auto_clear      = true;   // ← zeros DMA on underrun
  i2s_new_channel(&chan_cfg, &i2s_tx_chan, NULL);

  // 2. Standard I²S mono 16-bit
  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRCK,
      .dout = (gpio_num_t)I2S_DIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv   = false,
      },
    },
  };
  i2s_channel_init_std_mode(i2s_tx_chan, &std_cfg);

  // 3. Enable once and leave enabled — auto_clear handles silence
  i2s_channel_enable(i2s_tx_chan);
  Serial.println("[I2S] Initialized");
}