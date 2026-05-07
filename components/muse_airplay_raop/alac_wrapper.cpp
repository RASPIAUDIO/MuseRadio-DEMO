#include "alac_wrapper.h"

#include <new>
#include <stdlib.h>

#include "ALACBitUtilities.h"
#include "ALACDecoder.h"

namespace {
constexpr uint32_t kMaxAirPlayPacketBytes = 1408;
}

struct alac_codec_s {
  ALACDecoder *decoder;
  unsigned char sample_size;
  unsigned sample_rate;
  unsigned char channels;
  unsigned int block_size;
};

struct alac_codec_s *alac_create_decoder(int magic_cookie_size, unsigned char *magic_cookie,
                                         unsigned char *sample_size, unsigned *sample_rate,
                                         unsigned char *channels, unsigned int *block_size)
{
  if (!magic_cookie || magic_cookie_size <= 0) return nullptr;

  auto *codec = (struct alac_codec_s *)calloc(1, sizeof(struct alac_codec_s));
  if (!codec) return nullptr;

  codec->decoder = new (std::nothrow) ALACDecoder();
  if (!codec->decoder) {
    free(codec);
    return nullptr;
  }

  const int32_t status = codec->decoder->Init(magic_cookie, (uint32_t)magic_cookie_size);
  if (status != ALAC_noErr) {
    delete codec->decoder;
    free(codec);
    return nullptr;
  }

  codec->sample_size = codec->decoder->mConfig.bitDepth;
  codec->sample_rate = codec->decoder->mConfig.sampleRate;
  codec->channels = codec->decoder->mConfig.numChannels;
  codec->block_size = codec->decoder->mConfig.frameLength;

  if (sample_size) *sample_size = codec->sample_size;
  if (sample_rate) *sample_rate = codec->sample_rate;
  if (channels) *channels = codec->channels;
  if (block_size) *block_size = codec->block_size;

  return codec;
}

void alac_delete_decoder(struct alac_codec_s *codec)
{
  if (!codec) return;
  delete codec->decoder;
  free(codec);
}

bool alac_to_pcm(struct alac_codec_s *codec, unsigned char *input,
                 unsigned char *output, char channels, unsigned *out_frames)
{
  if (!codec || !codec->decoder || !input || !output || !out_frames) return false;

  BitBuffer bits;
  BitBufferInit(&bits, input, kMaxAirPlayPacketBytes);

  uint32_t decodedFrames = codec->block_size;
  const int32_t status = codec->decoder->Decode(&bits, output, codec->block_size,
                                                channels > 0 ? (uint32_t)channels : codec->channels,
                                                &decodedFrames);
  if (status != ALAC_noErr) {
    *out_frames = 0;
    return false;
  }

  *out_frames = decodedFrames;
  return true;
}

