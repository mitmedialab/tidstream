#ifndef __file_writer_h_
#define __file_writer_h_

#include <stdio.h>
#include <ogg/ogg.h>
#include <time.h>

#include "opus_header.h"

#define MIN_HIST 3840

typedef struct packet_hist {
    int bytes;
    int nframes;
    struct packet_hist *next;
    unsigned char packet[1];
} packet_hist;

typedef struct {
    OpusHeader id;
    char *tags;
    int tag_len;
    char *name;

    ogg_packet op;
    
    FILE *fd;
    ogg_stream_state os;

    int64_t granulepos; /* global sample position (not reset on new file) */
    int64_t max_length; /* maximum number of playable samples to write to a 
                           single file */

    int hist_frames;    /* total number of frames in the history buffer */
    int unused_frames;  /* frames in the history buffer that were not played as
                           part of the previous file */
    packet_hist *hist;  /* packet history queue */
    packet_hist *hist_last; /* convenience pointer to end of history queue */

    int filecount;
    char *timefmt;
    struct tm starttime;
} OpusFileWriter;

void file_writer_init(OpusFileWriter *fw, const char *name, const OpusHeader *id, 
  const char *tags, int tag_len);
void file_writer_input(OpusFileWriter *fw, ogg_packet *op);
void file_writer_close(OpusFileWriter *fw);

void file_writer_set_max_length(OpusFileWriter *fw, int64_t max_length);
void file_writer_update_granulepos(OpusFileWriter *fw, int64_t granulepos);


#endif // __file_writer_h_

