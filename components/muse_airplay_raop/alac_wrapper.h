#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct alac_codec_s;

struct alac_codec_s *alac_create_decoder(int magic_cookie_size, unsigned char *magic_cookie,
                                         unsigned char *sample_size, unsigned *sample_rate,
                                         unsigned char *channels, unsigned int *block_size);
void alac_delete_decoder(struct alac_codec_s *codec);
bool alac_to_pcm(struct alac_codec_s *codec, unsigned char *input,
                 unsigned char *output, char channels, unsigned *out_frames);

#ifdef __cplusplus
}
#endif

