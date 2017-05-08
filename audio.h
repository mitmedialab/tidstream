#ifndef __audio_h_
#define __audio_h_

#include <stdint.h>
#include <jack/jack.h>

void audio_connect_inputs(int offset);
int audio_process_cb(jack_nframes_t nframes, void *arg);
void audio_setup(const char *client_name, int channels);

int32_t audio_get_available(void);
void audio_get_data(float **data, int nframes);

void audio_interleave(float **data, float *interleaved, int channels, int nframes);

#endif // __audio_h_

