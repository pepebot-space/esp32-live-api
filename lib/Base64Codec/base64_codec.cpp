#include "base64_codec.h"
#include <mbedtls/base64.h>

String base64_encode_audio(const uint8_t *input, size_t input_len) {
  size_t olen = 0;
  mbedtls_base64_encode(NULL, 0, &olen, input, input_len);
  if (olen == 0)
    return "";

  char *buf = (char *)malloc(olen);
  if (!buf)
    return "";

  mbedtls_base64_encode((unsigned char *)buf, olen, &olen, input, input_len);
  String result = String(buf);
  free(buf);
  return result;
}

size_t base64_decode_audio(const String &input, uint8_t *output,
                           size_t output_buf_size) {
  if (input.length() == 0)
    return 0;

  String std_b64 = input;
  std_b64.replace('-', '+');
  std_b64.replace('_', '/');

  while (std_b64.length() % 4 != 0) {
    std_b64 += '=';
  }

  size_t olen = 0;
  int ret = mbedtls_base64_decode(output, output_buf_size, &olen,
                                  (const unsigned char *)std_b64.c_str(),
                                  std_b64.length());

  if (ret != 0) {
    Serial.printf("Base64 decode error: %d\n", ret);
    return 0;
  }

  return olen;
}
