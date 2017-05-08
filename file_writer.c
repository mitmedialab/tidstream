#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <opus/opus.h>

#include "file_writer.h"

void file_writer_init(OpusFileWriter *fw, const char *name, const OpusHeader *id, 
  const char *tags, int tag_len) {
    fw->name = (char*)malloc(strlen(name));
    strcpy(fw->name, name);
    
    memcpy(&fw->id, id, sizeof(OpusHeader));
    fw->tags = (char*)malloc(tag_len);
    memcpy(fw->tags, tags, tag_len);
    fw->tag_len = tag_len;

    fw->fd = NULL;

    fw->id.channels = 1;
    fw->id.channel_mapping = 0;
    ogg_stream_init(&fw->os, rand());
}

static void file_writer_write(OpusFileWriter *fw, bool flush) {
    ogg_page og;

    while(ogg_stream_pageout(&fw->os, &og) > 0) {
        fwrite(og.header, 1, og.header_len, fw->fd);
        fwrite(og.body, 1, og.body_len, fw->fd);
    }

    if(flush) {
        if(ogg_stream_flush(&fw->os, &og) > 0) {
            fwrite(og.header, 1, og.header_len, fw->fd);
            fwrite(og.body, 1, og.body_len, fw->fd);
        }
    }
}

void file_writer_input(OpusFileWriter *fw, ogg_packet *op) {
    if(fw->fd == NULL) {
        char namebuf[4096];
        snprintf(namebuf, sizeof(namebuf), "%s.opus", fw->name);
        fw->fd = fopen(namebuf, "wb");
        fprintf(stderr, "Opened output file %s\n", namebuf);

        ogg_stream_reset_serialno(&fw->os, rand());

        unsigned char id_buf[300];
        int id_size = opus_header_to_packet(&fw->id, id_buf, sizeof(id_buf));

        fw->op.packet = id_buf;
        fw->op.bytes = id_size;
        fw->op.b_o_s = 1;
        fw->op.e_o_s = 0;
        fw->op.packetno = 0;
        fw->op.granulepos = 0;

        ogg_stream_packetin(&fw->os, &fw->op);
        file_writer_write(fw, true);

        fw->op.packet = fw->tags;
        fw->op.bytes = fw->tag_len;
        fw->op.b_o_s = 0;
        fw->op.packetno++;
        
        ogg_stream_packetin(&fw->os, &fw->op);
        file_writer_write(fw, true);
    }

    int nframes = opus_packet_get_samples_per_frame(op->packet, 48000);

    fw->op.packet = op->packet;
    fw->op.bytes = op->bytes;
    fw->op.packetno++;
    fw->op.e_o_s = op->e_o_s;
    fw->op.granulepos += nframes;

    ogg_stream_packetin(&fw->os, &fw->op);
    file_writer_write(fw, false);
}

void file_writer_close(OpusFileWriter *fw) {
    if(!fw->fd) return;

    file_writer_write(fw, true);
    fclose(fw->fd);
    fw->fd = NULL;
}

