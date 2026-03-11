#ifndef I2S_MIC_H
#define I2S_MIC_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

bool i2s_mic_init();
void i2s_mic_start(RingbufHandle_t ring_buf);
void i2s_mic_stop();

#endif // I2S_MIC_H
