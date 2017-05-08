#include <opus/opus.h>
#include <opus/opus_multistream.h>
#include <ogg/ogg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <time.h>
#include "enc_opus.h"
#include "opus_header.h"

#define writeint(buf, base, val) { buf[base+3]=((val)>>24)&0xff; \
                                     buf[base+2]=((val)>>16)&0xff; \
                                     buf[base+1]=((val)>>8)&0xff; \
                                     buf[base]=(val)&0xff; \
                                 }

struct {
    ogg_stream_state os;

    ogg_page og;
    ogg_packet op;

    OpusMSEncoder *opus;
    unsigned char *data_out;
    int max_data_bytes;
    
    int n_channels;

    time_t last_stats;
} oo;

static int enc_opus_flush(shout_t *shout) {
    for(;;) {
        int result = ogg_stream_flush(&oo.os, &oo.og);
        if(result == 0) break;
        
        int ret = shout_send(shout, oo.og.header, oo.og.header_len);
        if(ret != SHOUTERR_SUCCESS) {
            fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
            return -4;
        }
        
        ret = shout_send(shout, oo.og.body, oo.og.body_len);
        if(ret != SHOUTERR_SUCCESS) {
            fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
            return -4;
        }
    }
    return 0;
}

int enc_opus_setup(shout_t *shout, int rate, int channels, int bitrate) {
    oo.last_stats = time(NULL);
    oo.n_channels = channels;

    srand(time(NULL));
    ogg_stream_init(&oo.os, rand());

    OpusHeader header;
    header.channels = channels;
    header.channel_mapping = 255;
    header.input_sample_rate = rate;
    header.gain = 0;
    header.nb_streams = channels;
    header.nb_coupled = 0;
    
    // allocate linear channel mapping
    for(int i=0; i<channels; i++) {
        header.stream_map[i] = i;
    }

    // ID Header
    unsigned char header_buf[300];
    int header_size = opus_header_to_packet(&header, header_buf, 300);

    oo.op.packet = header_buf;
    oo.op.bytes = header_size;
    oo.op.b_o_s = 1;
    oo.op.e_o_s = 0;
    oo.op.granulepos = 0;
    oo.op.packetno = 0;
    ogg_stream_packetin(&oo.os, &oo.op);
    enc_opus_flush(shout);

    // Comment header (why is there not a library that does this!?)
    char comment_buf[1024];
    memset(comment_buf, 0, sizeof(comment_buf));
    const char *vendor_string = opus_get_version_string();
    strcpy(comment_buf, "OpusTags");
    int p = 8;
    int vendor_length = strlen(vendor_string);
    writeint(comment_buf, p, vendor_length);
    p += 4;
    memcpy(&comment_buf[p], vendor_string, vendor_length);
    p += vendor_length;
    const char *encoder_string="ENCODER=tidstream";
    int encoder_length = strlen(encoder_string);
    writeint(comment_buf, p, 1);
    p += 4;
    writeint(comment_buf, p, encoder_length);
    p += 4;
    memcpy(&comment_buf[p], encoder_string, encoder_length);
    p += encoder_length;
    
    oo.op.packet = comment_buf;
    oo.op.bytes = p;
    oo.op.b_o_s = 0;
    oo.op.e_o_s = 0;
    oo.op.granulepos = 0;
    oo.op.packetno = 1;
    ogg_stream_packetin(&oo.os, &oo.op);
    enc_opus_flush(shout);

    // create encoder
    int error;
    oo.opus = opus_multistream_encoder_create(rate, oo.n_channels, 
        oo.n_channels, 0, header.stream_map, OPUS_APPLICATION_AUDIO, &error);
    if(error != OPUS_OK) {
        fprintf(stderr, "opus error\n");
        return -1;
    }

    int ret = opus_multistream_encoder_ctl(oo.opus, OPUS_SET_BITRATE(bitrate));
    if(ret != OPUS_OK) {
        fprintf(stderr, "failed to set bitrate: %s\n", opus_strerror(ret));
    }

    oo.max_data_bytes = (1275 * 3 + 7) * header.nb_streams;
    oo.data_out = malloc(oo.max_data_bytes * sizeof(unsigned char));

    return 0;
}

int enc_opus_encode(shout_t *shout, float *pcm, int nframes) {
    static int packets = 0;

    static int bytes_sent = 0;

    int bytes = opus_multistream_encode_float(oo.opus, pcm, nframes, oo.data_out,
        oo.max_data_bytes);
    if(bytes < 0) {
        fprintf(stderr, "opus encoding failed: %s\n", opus_strerror(bytes));
        return -1;
    }

    oo.op.packet = oo.data_out;
    oo.op.bytes = bytes;
    oo.op.packetno++;
    oo.op.granulepos += nframes;
    ogg_stream_packetin(&oo.os, &oo.op);
    packets++;

    bytes_sent += bytes;

    //printf("%d %d %d\n", oo.op.granulepos, oo.op.bytes, oo.op.packetno);


    for(;;) {
        int result = ogg_stream_pageout(&oo.os, &oo.og);
        if(result == 0) break;

        //printf("page\n");
        packets = 0;
        
        int ret = shout_send(shout, oo.og.header, oo.og.header_len);
        if(ret != SHOUTERR_SUCCESS) {
            fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
            return -4;
        }
        
        ret = shout_send(shout, oo.og.body, oo.og.body_len);
        if(ret != SHOUTERR_SUCCESS) {
            fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
            return -4;
        }
    }  


    if(packets > 16) {
        packets = 0;
        //printf("flush\n");
        enc_opus_flush(shout);
    }
    shout_sync(shout);

    time_t now = time(NULL);
    if(now - oo.last_stats > 2) {
        printf("  opus %d channels - % 8.02f kbps avg - %d packets        \r",
            oo.n_channels, 8 * bytes_sent / (float)(now - oo.last_stats) / 1000.,
            oo.op.packetno);
        oo.last_stats = now;
        bytes_sent = 0;
        fflush(stdout);
    }

    return 0;
}
