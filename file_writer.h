#ifndef __file_writer_h_
#define __file_writer_h_

#include <stdio.h>
#include <ogg/ogg.h>

#include "opus_header.h"

typedef struct {
    OpusHeader id;
    char *tags;
    int tag_len;
    char *name;

    ogg_packet op;
    
    FILE *fd;
    ogg_stream_state os;
} OpusFileWriter;

void file_writer_init(OpusFileWriter *fw, const char *name, const OpusHeader *id, 
  const char *tags, int tag_len);
void file_writer_input(OpusFileWriter *fw, ogg_packet *op);
void file_writer_close(OpusFileWriter *fw);


#endif // __file_writer_h_

