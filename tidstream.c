#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "audio.h"
#include "enc_vorbis.h"
#include "enc_opus.h"
#include "stream.h"

typedef enum {
    CODEC_VORBIS,
    CODEC_OPUS
} codec_mode_t;

codec_mode_t codec = CODEC_VORBIS;
const char *client_name = "tidstream";
int n_channels = 8;
int min_bitrate = 128000;
int max_bitrate = 384000;
int avg_bitrate = 256000;
const char *shout_host = "doppler.media.mit.edu";
const char *shout_mount = "tidmarsh_test.ogg";
const char *shout_password = "password";
int shout_port = 8000;
int chunk_size = 4096;
int auto_connect = 0;
int auto_connect_offset=1;
int retry = 0;

void show_help(int argc, char **argv) {
    printf("usage: %s <options>\n", argv[0]);
    printf("");
    printf("    -A (autoconnect jack)\n");
    printf("    -O (connect offset) (%d)\n", auto_connect_offset);
    printf("    -r (retry on error)\n");
    printf("    -c <channels>       (%d)\n", n_channels);
    printf("    -h <hostname>       (%s)\n", shout_host);
    printf("    -p <port>           (%d)\n", shout_port);
    printf("    -u <mountpoint>     (%s)\n", shout_mount);
    printf("    -w <password>       (%s)\n", shout_password);
    printf("    -m <min bitrate>    (%d)\n", min_bitrate / 1000);
    printf("    -a <avg bitrate>    (%d)\n", avg_bitrate / 1000);
    printf("    -x <max bitrate>    (%d)\n", max_bitrate / 1000);
    printf("    -o (use opus)           \n");
}

typedef enum {
    ERR_OK,
    ERR_ENCODER_SETUP,
    ERR_STREAM_SETUP,
    ERR_STREAM,
    ERR_ENCODER,
} tidstream_err_status_t;

const char *status_str(tidstream_err_status_t status) {
    switch(status) {
        case ERR_OK: return "OK";
        case ERR_ENCODER_SETUP: "encoder setup error";
        case ERR_STREAM_SETUP: "stream setup error";
        case ERR_STREAM: "streaming error";
        case ERR_ENCODER: "encoder/streaming error";
        default: return "unknown error";
    }
}

bool check_retry(tidstream_err_status_t status) {
    if(retry) {
        fprintf(stderr, "main loop terminated due to error: %s\n", 
            status_str(status));
        fprintf(stderr, "restarting after delay\n");
        sleep(10);
        return true;
    } else {
        return false;
    }
}

int main(int argc, char **argv) {
    float **data;
    float *interleaved;
    char c;

    opterr = 0;
    while((c = getopt(argc, argv, "AO:c:h:p:u:w:m:a:x:or")) != -1) {
        switch(c) {
            case 'A':
                auto_connect = 1;
                break;
            case 'O':
                auto_connect_offset = atoi(optarg);
                break;
            case 'c':
                n_channels = atoi(optarg);
                break;
            case 'h':
                shout_host = optarg;
                break;
            case 'p':
                shout_port = atoi(optarg);
                break;
            case 'u':
                shout_mount = optarg;
                break;
            case 'w':
                shout_password = optarg;
                break;
            case 'm':
                min_bitrate = atoi(optarg) * 1000;
                break;
            case 'a':
                avg_bitrate = atoi(optarg) * 1000;
                break;
            case 'x':
                max_bitrate = atoi(optarg) * 1000;
                break;
            case '?':
                fprintf(stderr, "error parsing option -%c\n", optopt);
                break;
            case 'o':
                codec = CODEC_OPUS;
                chunk_size = 960;
                break;
            case 'r':
                retry = 1;
                break;
            default:
                abort();
        }
    }

    show_help(argc, argv);

    data = malloc(sizeof(float*) * n_channels);
    for(int i=0; i<n_channels; i++) {
        data[i] = malloc(sizeof(float) * chunk_size);
    }

    if(codec == CODEC_OPUS) {
        interleaved = malloc(sizeof(float) * n_channels * chunk_size);
    }

    audio_setup(client_name, n_channels);

    if(auto_connect) {
        audio_connect_inputs(auto_connect_offset);
    }

    tidstream_err_status_t status = ERR_OK;

    do {
        reinitialize:
        status = ERR_OK;

        shout_t *shout = stream_setup(shout_host, shout_port, shout_password,
            shout_mount);
        if(!shout) {
            fprintf(stderr, "shout error\n");
            status = ERR_STREAM_SETUP;
            continue;
        }

        if(codec == CODEC_OPUS) {
            int ret = enc_opus_setup(shout, 48000, n_channels, avg_bitrate);
            if(ret != 0) {
                fprintf(stderr, "enc_opus_setup error\n");
                status = ERR_ENCODER_SETUP;
                continue;
            }
        } else {
            int ret = enc_vorbis_setup(shout, 48000, n_channels, min_bitrate,
                avg_bitrate, max_bitrate);
            if(ret != 0) {
                fprintf(stderr, "vorbis error\n");
                status = ERR_ENCODER_SETUP;
                continue;
            }
        }


        for(;;) {
            int ret = 0;
            if(audio_get_available() > chunk_size * sizeof(float)) {
                if(codec == CODEC_OPUS) {
                    audio_get_data(data, chunk_size);
                    audio_interleave(data, interleaved, n_channels, chunk_size);
                    ret = enc_opus_encode(shout, interleaved, chunk_size);
                } else {
                    audio_get_data(data, chunk_size);
                    ret = enc_vorbis_encode(shout, data, chunk_size);
                }
                if(ret != 0) {
                    fprintf(stderr, "encoder error: %d\n", ret);
                    status = ERR_ENCODER;
                    goto reinitialize;
                }
            }
            usleep(1000);
        }
    } while(check_retry(status));

    return status;
}

