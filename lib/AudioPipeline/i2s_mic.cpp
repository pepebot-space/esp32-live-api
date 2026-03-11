#include "i2s_mic.h"
#include "config.h"
#include <driver/i2s.h>

static TaskHandle_t _mic_task_handle = NULL;
static RingbufHandle_t _mic_ring_buf = NULL;

static void mic_task(void *param) {
  int16_t buffer[MIC_CHUNK_SAMPLES];
  size_t bytes_read;

  while (true) {
    esp_err_t err = i2s_read(I2S_MIC_PORT, buffer, MIC_CHUNK_BYTES, &bytes_read,
                             portMAX_DELAY);

    if (err == ESP_OK && bytes_read > 0) {
      // Apply gain
      for (size_t i = 0; i < bytes_read / 2; i++) {
#ifdef MIC_GAIN_SHIFT
        int32_t sample = buffer[i] << MIC_GAIN_SHIFT;
#else
        int32_t sample = buffer[i] << 2; // 12dB gain
#endif
        // Clamp to prevent overflow
        if (sample > 32767)
          sample = 32767;
        if (sample < -32768)
          sample = -32768;
        buffer[i] = (int16_t)sample;
      }

      if (_mic_ring_buf) {
        xRingbufferSend(_mic_ring_buf, buffer, bytes_read, pdMS_TO_TICKS(100));
      }
    }
  }
}

bool i2s_mic_init() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = I2S_MIC_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_MIC_SCK,
      .ws_io_num = I2S_MIC_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_MIC_SD,
  };

  esp_err_t err = i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed installing mic I2S driver: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_MIC_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed setting mic I2S pins: %d\n", err);
    return false;
  }

  return true;
}

void i2s_mic_start(RingbufHandle_t ring_buf) {
  _mic_ring_buf = ring_buf;
  if (_mic_task_handle == NULL) {
    xTaskCreatePinnedToCore(mic_task, "mic_task", 8192, NULL, 10,
                            &_mic_task_handle, 1);
  }
}

void i2s_mic_stop() {
  if (_mic_task_handle != NULL) {
    vTaskDelete(_mic_task_handle);
    _mic_task_handle = NULL;
  }
}
