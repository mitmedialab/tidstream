#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "circbuf.h"
#include "audio.h"

#define AUDIO_BUFFER_SIZE 48000

int n_channels;
circbuf_t **channel_buffers;
jack_client_t *jack_client;
jack_port_t **ports_in;
jack_nframes_t audio_srate;
const char *cname;

static int audio_srate_change_cb(jack_nframes_t nframes, void *arg) {
    fprintf(stderr, "audio: sample rate changed to %lu Hz\n", nframes);
    audio_srate = nframes;
    return 0;
}

static void audio_error_cb(const char *desc) {
    fprintf(stderr, "audio: JACK error: %s\n", desc);
}

static void audio_jack_shutdown_cb(void *arg) {
    fprintf(stderr, "audio: JACK shutdown\n");
}

void audio_connect_inputs(int offset) {
    char src[256];
    char dst[256];

    for(int i=0; i<n_channels; i++) {
        snprintf(src, sizeof(src), "system:capture_%d", i+offset);
        snprintf(dst, sizeof(dst), "%s:in_%d", cname, i+1);
        int ret = jack_connect(jack_client, src, dst);
        if(ret) {
            fprintf(stderr, "error connecting %s -> %s\n", src, dst);
        } else {
            fprintf(stderr, "connected %s -> %s\n", src, dst);
        }
    }
}

int audio_process_cb(jack_nframes_t nframes, void *arg) {
    int32_t length = nframes * sizeof(jack_default_audio_sample_t);
    for(int i=0; i<n_channels; i++) {
        jack_default_audio_sample_t *ch =
            (jack_default_audio_sample_t*)jack_port_get_buffer(
                ports_in[i], nframes);
        if(circbuf_write(channel_buffers[i], ch, length) < length) {
            fprintf(stderr, "buffer overrun (%d)\n", i);
        }
    }
    return 0;
}

void audio_setup(const char *client_name, int channels) {
    n_channels = channels;
    cname = client_name;

    jack_set_error_function(audio_error_cb);

    if(!(jack_client = jack_client_open(client_name, 0, NULL))) {
        fprintf(stderr, "cannot connect to JACK\n");
        exit(2);
    }

    jack_set_process_callback(jack_client, audio_process_cb, NULL);
    jack_set_sample_rate_callback(jack_client, audio_srate_change_cb, NULL);
    jack_on_shutdown(jack_client, audio_jack_shutdown_cb, NULL);

    channel_buffers = malloc(sizeof(circbuf_t*) * n_channels);
    ports_in = malloc(sizeof(jack_port_t*) * n_channels);

    for(int i=0; i<n_channels; i++) {
        channel_buffers[i] = circbuf_new(AUDIO_BUFFER_SIZE * 
            sizeof(jack_default_audio_sample_t));
        if(!channel_buffers[i]) {
            fprintf(stderr, "cannot allocate channel buffers\n");
            exit(1);
        }
        
        char port_name[128];
        snprintf(port_name, sizeof(port_name), "in_%d", i+1);

        ports_in[i] = jack_port_register(jack_client, port_name, 
            JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    }

    cname = jack_get_client_name(jack_client);

    if(jack_activate(jack_client)) {
        fprintf(stderr, "cannot activate JACK client\n");
        exit(3);
    }
}

int32_t audio_get_available(void) {
    return circbuf_get_available(channel_buffers[0]);
}

void audio_get_data(float **data, int nframes) {
    for(int i=0; i<n_channels; i++) {
        circbuf_read(channel_buffers[i], data[i], 
            nframes * sizeof(jack_default_audio_sample_t));
    }
}

void audio_interleave(float **data, float *interleaved, int channels, int nframes) {
    int c = 0;
    int s = 0;
    for(int i=0; i<channels*nframes; i++) {
        interleaved[i] = data[c++][s];
        if(c == channels) {
            c = 0;
            s++;
        }
    }
}

