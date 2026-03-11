#ifndef I2S_SPEAKER_H
#define I2S_SPEAKER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

bool i2s_speaker_init();
void i2s_speaker_start(RingbufHandle_t ring_buf);
void i2s_speaker_stop();

#endif // I2S_SPEAKER_H
