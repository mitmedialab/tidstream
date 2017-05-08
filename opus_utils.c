#include <stdlib.h>
#include <stdio.h>
#include "opus_utils.h"

int opus_parse_size(const unsigned char *data, opus_int32 len, opus_int16 *size)
{
   if (len<1)
   {
      *size = -1;
      return -1;
   } else if (data[0]<252)
   {
      *size = data[0];
      return 1;
   } else if (len<2)
   {
      *size = -1;
      return -1;
   } else {
      *size = 4*data[1] + data[0];
      return 2;
   }
}


int opus_packet_parse_impl(const unsigned char *data, opus_int32 len,
      int self_delimited, unsigned char *out_toc,
      const unsigned char *frames[48], opus_int16 size[48],
      int *payload_offset, opus_int32 *packet_offset)
{
   int i, bytes;
   int count;
   int cbr;
   unsigned char ch, toc;
   int framesize;
   opus_int32 last_size;
   opus_int32 pad = 0;
   const unsigned char *data0 = data;

   if (size==NULL || len<0)
      return OPUS_BAD_ARG;
   if (len==0)
      return OPUS_INVALID_PACKET;

   framesize = opus_packet_get_samples_per_frame(data, 48000);

   cbr = 0;
   toc = *data++;
   len--;
   last_size = len;
   switch (toc&0x3)
   {
   /* One frame */
   case 0:
      count=1;
      break;
   /* Two CBR frames */
   case 1:
      count=2;
      cbr = 1;
      if (!self_delimited)
      {
         if (len&0x1)
            return OPUS_INVALID_PACKET;
         last_size = len/2;
         /* If last_size doesn't fit in size[0], we'll catch it later */
         size[0] = (opus_int16)last_size;
      }
      break;
   /* Two VBR frames */
   case 2:
      count = 2;
      bytes = opus_parse_size(data, len, size);
      len -= bytes;
      if (size[0]<0 || size[0] > len)
         return OPUS_INVALID_PACKET;
      data += bytes;
      last_size = len-size[0];
      break;
   /* Multiple CBR/VBR frames (from 0 to 120 ms) */
   default: /*case 3:*/
      if (len<1)
         return OPUS_INVALID_PACKET;
      /* Number of frames encoded in bits 0 to 5 */
      ch = *data++;
      count = ch&0x3F;
      if (count <= 0 || framesize*count > 5760)
         return OPUS_INVALID_PACKET;
      len--;
      /* Padding flag is bit 6 */
      if (ch&0x40)
      {
         int p;
         do {
            int tmp;
            if (len<=0)
               return OPUS_INVALID_PACKET;
            p = *data++;
            len--;
            tmp = p==255 ? 254: p;
            len -= tmp;
            pad += tmp;
         } while (p==255);
      }
      if (len<0)
         return OPUS_INVALID_PACKET;
      /* VBR flag is bit 7 */
      cbr = !(ch&0x80);
      if (!cbr)
      {
         /* VBR case */
         last_size = len;
         for (i=0;i<count-1;i++)
         {
            bytes = opus_parse_size(data, len, size+i);
            len -= bytes;
            if (size[i]<0 || size[i] > len)
               return OPUS_INVALID_PACKET;
            data += bytes;
            last_size -= bytes+size[i];
         }
         if (last_size<0)
            return OPUS_INVALID_PACKET;
      } else if (!self_delimited)
      {
         /* CBR case */
         last_size = len/count;
         if (last_size*count!=len)
            return OPUS_INVALID_PACKET;
         for (i=0;i<count-1;i++)
            size[i] = (opus_int16)last_size;
      }
      break;
   }
   /* Self-delimited framing has an extra size for the last frame. */
   if (self_delimited)
   {
      bytes = opus_parse_size(data, len, size+count-1);
      len -= bytes;
      if (size[count-1]<0 || size[count-1] > len)
         return OPUS_INVALID_PACKET;
      data += bytes;
      /* For CBR packets, apply the size to all the frames. */
      if (cbr)
      {
         if (size[count-1]*count > len)
            return OPUS_INVALID_PACKET;
         for (i=0;i<count-1;i++)
            size[i] = size[count-1];
      } else if (bytes+size[count-1] > last_size)
         return OPUS_INVALID_PACKET;
   } else
   {
      /* Because it's not encoded explicitly, it's possible the size of the
         last packet (or all the packets, for the CBR case) is larger than
         1275. Reject them here.*/
      if (last_size > 1275)
         return OPUS_INVALID_PACKET;
      size[count-1] = (opus_int16)last_size;
   }

   if (payload_offset)
      *payload_offset = (int)(data-data0);

   for (i=0;i<count;i++)
   {
      if (frames)
         frames[i] = data;
      data += size[i];
   }

   if (packet_offset)
      *packet_offset = pad+(opus_int32)(data-data0);

   if (out_toc)
      *out_toc = toc;

   return count;
}

int opus_multistream_packet_validate(const unsigned char *data,
      opus_int32 len, int nb_streams, opus_int32 Fs)
{
   int s;
   int count;
   unsigned char toc;
   opus_int16 size[48];
   int samples=0;
   opus_int32 packet_offset;

   for (s=0;s<nb_streams;s++)
   {
      int tmp_samples;
      if (len<=0)
         return OPUS_INVALID_PACKET;
      count = opus_packet_parse_impl(data, len, s!=nb_streams-1, &toc, NULL,
                                     size, NULL, &packet_offset);
      if (count<0)
         return count;
      tmp_samples = opus_packet_get_nb_samples(data, packet_offset, Fs);
      if (s!=0 && samples != tmp_samples)
         return OPUS_INVALID_PACKET;
      samples = tmp_samples;
      data += packet_offset;
      len -= packet_offset;
   }
   return samples;
}



