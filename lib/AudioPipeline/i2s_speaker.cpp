#include "i2s_speaker.h"
#include "config.h"
#include <driver/i2s.h>

static TaskHandle_t _speaker_task_handle = NULL;
static RingbufHandle_t _spk_ring_buf = NULL;

static void speaker_task(void *param) {
  size_t item_size;
  size_t bytes_written;
  int16_t silence[256] = {0};

  while (true) {
    if (_spk_ring_buf) {
      void *item =
          xRingbufferReceive(_spk_ring_buf, &item_size, pdMS_TO_TICKS(50));
      if (item != NULL) {
        i2s_write(I2S_SPK_PORT, item, item_size, &bytes_written, portMAX_DELAY);
        vRingbufferReturnItem(_spk_ring_buf, item);
      } else {
        // Keep the I2S clock running with silence to prevent pop noise
        i2s_write(I2S_SPK_PORT, silence, sizeof(silence), &bytes_written,
                  pdMS_TO_TICKS(10));
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

bool i2s_speaker_init() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = I2S_SPK_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SPK_BCLK,
      .ws_io_num = I2S_SPK_LRC,
      .data_out_num = I2S_SPK_DOUT,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  esp_err_t err = i2s_driver_install(I2S_SPK_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed installing speaker I2S driver: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_SPK_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed setting speaker I2S pins: %d\n", err);
    return false;
  }

  // Zero out the DMA buffer
  i2s_zero_dma_buffer(I2S_SPK_PORT);

  return true;
}

void i2s_speaker_start(RingbufHandle_t ring_buf) {
  _spk_ring_buf = ring_buf;
  if (_speaker_task_handle == NULL) {
    // Run on core 1 so it's isolated
    xTaskCreatePinnedToCore(speaker_task, "speaker_task", 4096, NULL, 10,
                            &_speaker_task_handle, 1);
  }
}

void i2s_speaker_stop() {
  if (_speaker_task_handle != NULL) {
    vTaskDelete(_speaker_task_handle);
    _speaker_task_handle = NULL;
  }
}
