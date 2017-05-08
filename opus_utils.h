#ifndef __opus_utils_h_
#define __opus_utils_h_

#include <opus/opus.h>

int opus_parse_size(const unsigned char *data, opus_int32 len, opus_int16 *size);
int opus_multistream_packet_validate(const unsigned char *data,
      opus_int32 len, int nb_streams, opus_int32 Fs);
int opus_packet_parse_impl(const unsigned char *data, opus_int32 len,
      int self_delimited, unsigned char *out_toc,
      const unsigned char *frames[48], opus_int16 size[48],
      int *payload_offset, opus_int32 *packet_offset);

#endif // __opus_utils_h_

