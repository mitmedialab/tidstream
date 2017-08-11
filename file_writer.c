#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <opus/opus.h>

#include "util.h"
#include "file_writer.h"

void file_writer_init(OpusFileWriter *fw, const char *name, const OpusHeader *id,
  const char *tags, int tag_len) {
    fw->name = (char*)malloc(strlen(name));
    CHECK_MALLOC(fw->name);
    strcpy(fw->name, name);

    memcpy(&fw->id, id, sizeof(OpusHeader));
    fw->tags = (char*)malloc(tag_len);
    CHECK_MALLOC(fw->tags);
    memcpy(fw->tags, tags, tag_len);
    fw->tag_len = tag_len;

    fw->fd = NULL;

    fw->id.channels = id->channels;
    fw->id.channel_mapping = id->channel_mapping;
    ogg_stream_init(&fw->os, rand());

    fw->granulepos = 0;
    fw->max_length = 3600 * id->input_sample_rate;
    fw->unused_frames = 0;
    fw->hist = NULL;
    fw->hist_last = NULL;
    fw->hist_frames = 0;

    fw->filecount = 0;
    fw->timefmt = NULL;
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
        if(fw->max_length > 0) {
            snprintf(namebuf, sizeof(namebuf), "%s-%d.opus", fw->name, fw->filecount++);
        } else {
            snprintf(namebuf, sizeof(namebuf), "%s.opus", fw->name);
        }
        fw->fd = fopen(namebuf, "wb");

        ogg_stream_reset_serialno(&fw->os, rand());

        if(fw->hist != NULL) {
            fw->id.preskip = fw->hist_frames - fw->unused_frames;
            fw->unused_frames = 0;
        }

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

        /* write history buffer to file (used to help decoder converge before
         * decoded samples appear */
        for(packet_hist *pkt = fw->hist; pkt != NULL; pkt = pkt->next) {
            fw->op.packet = pkt->packet;
            fw->op.bytes = pkt->bytes;
            fw->op.packetno++;
            fw->op.e_o_s = 0;
            fw->op.granulepos += pkt->nframes;

            ogg_stream_packetin(&fw->os, &fw->op);
            file_writer_write(fw, false);
        }
    }

    int nframes = opus_packet_get_samples_per_frame(op->packet, 48000);

    /* Manage the history queue: create the new packet */
    packet_hist *cur_packet = (packet_hist*)malloc(sizeof(packet_hist) + op->bytes);
    CHECK_MALLOC(cur_packet);
    cur_packet->bytes = op->bytes;
    cur_packet->nframes = nframes;
    cur_packet->next = NULL;
    memcpy(&cur_packet->packet[0], op->packet, op->bytes);

    /* Insert at end of queue */
    if(fw->hist == NULL) {
        fw->hist = cur_packet;
    } else {
        fw->hist_last->next = cur_packet;
    }
    fw->hist_last = cur_packet;
    fw->hist_frames += cur_packet->nframes;

    /* Write packet out */
    fw->op.packet = op->packet;
    fw->op.bytes = op->bytes;
    fw->op.packetno++;
    fw->op.e_o_s = op->e_o_s;
    fw->op.granulepos += nframes;
    fw->granulepos += nframes;

    bool close = false;

    if(fw->max_length > 0) {
        /* Will writing this packet reach or exceed maximum file length? */
        if(fw->op.granulepos >= fw->id.preskip + fw->max_length) {
            fw->unused_frames = fw->op.granulepos - (fw->id.preskip + fw->max_length);
            fw->op.granulepos = fw->id.preskip + fw->max_length;
            fw->op.e_o_s = 1;
            fw->granulepos -= fw->unused_frames;
            close = true;
        }
    }

    ogg_stream_packetin(&fw->os, &fw->op);
    file_writer_write(fw, close);

    if(close) {
        file_writer_close(fw);
    }

    /* Trim queue */
    while((fw->hist_frames - fw->hist->nframes) >= (MIN_HIST + fw->unused_frames)) {
        fw->hist_frames -= fw->hist->nframes;
        packet_hist *hist_del = fw->hist;
        fw->hist = fw->hist->next;
        free(hist_del);
    }
}

void file_writer_close(OpusFileWriter *fw) {
    if(!fw->fd) return;

    file_writer_write(fw, true);
    fclose(fw->fd);
    fw->fd = NULL;
}

void file_writer_set_max_length(OpusFileWriter *fw, int64_t max_length) {
    fw->max_length = max_length;
}

void file_writer_update_granulepos(OpusFileWriter *fw, int64_t granulepos) {
    fw->granulepos = granulepos;
}
