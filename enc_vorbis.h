#ifndef __enc_vorbis_h_
#define __enc_vorbis_h_

#include <shout/shout.h>

int enc_vorbis_setup(shout_t *shout, int rate, int channels, int min_bitrate, 
    int avg_bitrate, int max_bitrate);
int enc_vorbis_encode(shout_t *shout, float **data, int nframes);

#endif // __enc_vorbis_h_

