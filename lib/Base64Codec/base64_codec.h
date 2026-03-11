#ifndef BASE64_CODEC_H
#define BASE64_CODEC_H

#include <Arduino.h>

// Encode raw PCM bytes to base64 String (URL-safe conversion included if
// needed)
String base64_encode_audio(const uint8_t *input, size_t input_len);

// Decode base64 string to raw bytes
// Handle URL-safe base64: replace '-' with '+', '_' with '/' and pad
size_t base64_decode_audio(const String &input, uint8_t *output,
                           size_t output_buf_size);

#endif // BASE64_CODEC_H
