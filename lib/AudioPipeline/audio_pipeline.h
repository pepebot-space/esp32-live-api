#ifndef AUDIO_PIPELINE_H
#define AUDIO_PIPELINE_H

#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

extern RingbufHandle_t mic_ring_buf;
extern RingbufHandle_t spk_ring_buf;

bool audio_pipeline_init();

#endif // AUDIO_PIPELINE_H
