#include "audio_pipeline.h"
#include "config.h"

RingbufHandle_t mic_ring_buf = NULL;
RingbufHandle_t spk_ring_buf = NULL;

bool audio_pipeline_init() {
  // Ringbuf size must be carefully considered because of SRAM limits
  mic_ring_buf = xRingbufferCreate(MIC_RING_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
  if (mic_ring_buf == NULL) {
    Serial.println("Failed to create mic ring buffer");
    return false;
  }

  spk_ring_buf = xRingbufferCreate(SPK_RING_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
  if (spk_ring_buf == NULL) {
    Serial.println("Failed to create speaker ring buffer");
    return false;
  }

  return true;
}
