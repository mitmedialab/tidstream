#ifndef __enc_opus_h_
#define __enc_opus_h_

#include <shout/shout.h>

int enc_opus_setup(shout_t *shout, int rate, int channels, int bitrate);
int enc_opus_encode(shout_t *shout, float *pcm, int nframes);

#endif // __enc_opus_h_

