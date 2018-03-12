#include <vorbis/vorbisenc.h>
#include <ogg/ogg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "enc_vorbis.h"

struct {
    ogg_stream_state os; /* take physical pages, weld into a logical
                          stream of packets */
    ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
    ogg_packet       op; /* one raw packet of data for decode */
    vorbis_info      vi; /* struct that stores all the static vorbis bitstream
                          settings */
    vorbis_comment   vc; /* struct that stores all the user comments */
    vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
    vorbis_block     vb; /* local working space for packet->PCM decode */

    int n_channels;
} ov;

int enc_vorbis_setup(shout_t *shout, int rate, int channels, int min_bitrate, 
  int avg_bitrate, int max_bitrate) {
    ov.n_channels = channels;
    int ret;

    vorbis_info_init(&ov.vi);
    ret = vorbis_encode_init(&ov.vi, ov.n_channels, rate, max_bitrate, avg_bitrate,
        min_bitrate);
    if(ret) {
        fprintf(stderr, "fatal vorbis error\n");
        return -3;
    }

    vorbis_comment_init(&ov.vc);
    vorbis_comment_add_tag(&ov.vc, "ENCODER", "tidstream");

    vorbis_analysis_init(&ov.vd, &ov.vi);
    vorbis_block_init(&ov.vd, &ov.vb);

    srand(time(NULL));
    ogg_stream_init(&ov.os, rand());

    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;

    vorbis_analysis_headerout(&ov.vd, &ov.vc, &header, &header_comm, &header_code);
    ogg_stream_packetin(&ov.os, &header);
    ogg_stream_packetin(&ov.os, &header_comm);
    ogg_stream_packetin(&ov.os, &header_code);

    for(;;) {
        int result = ogg_stream_flush(&ov.os, &ov.og);
        if(result == 0) break;

        ret = shout_send(shout, ov.og.header, ov.og.header_len);
        if(ret != SHOUTERR_SUCCESS) {
            fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
            return -4;
        }

        ret = shout_send(shout, ov.og.body, ov.og.body_len);
        if(ret != SHOUTERR_SUCCESS) {
            fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
            return -4;
        }
    }

    return 0;
}

int enc_vorbis_encode(shout_t *shout, float **data, int nframes) {
    float **vorbis_input = vorbis_analysis_buffer(&ov.vd, nframes);
    for(int i=0; i<ov.n_channels; i++) {
        memcpy(vorbis_input[i], data[i], nframes * sizeof(float));
    }
    vorbis_analysis_wrote(&ov.vd, nframes);

    while(vorbis_analysis_blockout(&ov.vd, &ov.vb) == 1) {
        vorbis_analysis(&ov.vb, NULL);
        vorbis_bitrate_addblock(&ov.vb);

        while(vorbis_bitrate_flushpacket(&ov.vd, &ov.op)) {
            ogg_stream_packetin(&ov.os, &ov.op);

            int eos = 0;
            while(!eos) {
                int result = ogg_stream_pageout(&ov.os, &ov.og);
                if(result == 0) break;

                int ret = shout_send(shout, ov.og.header, ov.og.header_len);
                if(ret != SHOUTERR_SUCCESS) {
                    fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
                    return -4;
                }
                
                ret = shout_send(shout, ov.og.body, ov.og.body_len);
                if(ret != SHOUTERR_SUCCESS) {
                    fprintf(stderr, "shout error: %s\n", shout_get_error(shout));
                    return -4;
                }

                shout_sync(shout);
            }

            if(ogg_page_eos(&ov.og)) {
                eos = 1;
            }
        }
    }
}
