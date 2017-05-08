#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>

#include "opus_header.h"
#include "opus_utils.h"
#include "file_writer.h"

#define readint(buf, base) ( (buf[base]) + (buf[base+1] << 8) + \
                             (buf[base+2] << 16) + (buf[base+3] << 24) );

void usage(char *exe) {
    fprintf(stderr, "usage: %s [options] infile.opus\n", exe);
}

/**
 * Decodes the Opus comment header. (Quick and dirty; this will leak memory).
 * @param buf the contents of the header packet
 * @param length the length of the comment header data
 * @return an array of strings on success, where the first string is the vendor
 *  and the remaining strings are KEY=VALUE tags.  NULL if the header could not
 *  be decoded/was invalid.
 */
char **decode_comment_header(char *buf, int length) {
    if(length < 12) return NULL;
    if(strncmp("OpusTags", buf, 8) != 0) return 0;
    int p = 8;

    int vendor_length = readint(buf, p);
    p += 4;
    if(p + vendor_length > length) return NULL;
    char *vendor = (char*)malloc(vendor_length);
    memcpy(vendor, buf+p, vendor_length);
    p += vendor_length;

    if(p + 4 > length) return NULL;
    int ntags = readint(buf, p);
    p += 4;

    char **comments = (char**)malloc((ntags + 2) * sizeof(char*));
    comments[0] = vendor;
    comments[ntags] = NULL;

    for(int i=0; i<ntags; i++) {
        if(p + 4 > length) return NULL;
        int taglen = readint(buf, p);
        p += 4;
        if(p + taglen > length) return NULL;
        comments[i+1] = (char*)malloc(taglen);
        memcpy(comments[i+1], buf+p, taglen);
        p += taglen;
    }

    return comments;
}


int main(int argc, char **argv) {
    int status = 0;
    char *filename_base = NULL;

    if(optind >= argc) {
        fprintf(stderr, "error: no input file specified\n");
        usage(argv[0]);
        return 2;
    }

    char *filename = argv[optind];

    FILE *fp = fopen(filename, "rb");
    if(!fp) {
        perror("error: opening file:");
        return 10;
    }

    ogg_sync_state oy;
    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;

    OpusHeader *header = NULL;
    char *comment_header = NULL;
    int comment_length = 0;

    ogg_sync_init(&oy);
    bool stream_init = false;
    
    printf("Reading file: %s\n", filename);

    bool have_headers = false;


    while(!have_headers) {
        if(feof(fp)) {
            fprintf(stderr, "error: end of file reached before header found\n");
            status = 11;
            goto cleanup;
        }

        //printf("read\n");
        char *buf = ogg_sync_buffer(&oy, 4096);
        size_t n = fread(buf, 1, 4096, fp);
        int err = ogg_sync_wrote(&oy, n);
        if(err) {
            fprintf(stderr, "error: ogg sync error\n");
        }
    
        while((ogg_sync_pageout(&oy, &og) == 1) && !have_headers) {
            if(!stream_init) {
                ogg_stream_init(&os, ogg_page_serialno(&og));
                stream_init = true;
            }

            err = ogg_stream_pagein(&os, &og);
            if(err < 0)  {
                fprintf(stderr, "error: ogg stream error\n");
                continue;
            }

            printf("page\n");

            while((err = ogg_stream_packetout(&os, &op)) && !have_headers) {
                if(err == -1) {
                    fprintf(stderr, "warning: gap in stream\n");
                    continue;
                }

                if(!header) {
                    header = (OpusHeader*)malloc(sizeof(OpusHeader));
                    if(!opus_header_parse(op.packet, op.bytes, header)) {
                        fprintf(stderr, "error: not a usable opus stream\n");
                        goto cleanup;
                    }

                    continue;
                }

                if(!comment_header) {
                    comment_header = (char*)malloc(op.bytes);
                    comment_length = op.bytes;
                    memcpy(comment_header, op.packet, op.bytes);
                    
                    have_headers = true;

                    continue;
                }
            }

        }
    }

    printf("Opus header:\n");
    printf("  number of channels:   %d\n", header->channels);
    printf("             preskip:   %d samples\n", header->preskip);
    printf("         sample rate:   %d Hz\n", header->input_sample_rate);
    printf("                gain:   %0.02f dB\n", header->gain / 256.0f);
    printf("     channel mapping:   %d\n", header->channel_mapping);
    printf("   number of streams:   %d streams\n", header->nb_streams);
    printf("   number of coupled:   %d streams\n", header->nb_coupled);
    printf("\n");
    printf("Comments:\n");
    char **comments = decode_comment_header(comment_header, comment_length);
    if(comments) {
        for(int i=0; comments[i] != NULL; i++) {
            printf("  %s\n", comments[i]);
        }
    } else {
        printf("  failed to decode comment header\n");
    }

    char basename[4096];
    strncpy(basename, filename, sizeof(basename)-1);
    if(strlen(filename) > 5 && strcmp(".opus", filename + (strlen(filename) - 5)) == 0) {
        basename[strlen(basename) - 5] = '\0';
    }


    printf("Creating file writers\n");
    OpusFileWriter **file_writers = (OpusFileWriter**)malloc(header->nb_streams * 
        sizeof(OpusFileWriter*));
    for(int i=0; i<header->nb_streams; i++) {
        file_writers[i] = (OpusFileWriter*)malloc(sizeof(OpusFileWriter));
        char namebuf[4096];
        snprintf(namebuf, sizeof(namebuf), "%s-%02d", basename, i+1);
        file_writer_init(file_writers[i], namebuf, header, comment_header, comment_length);
    }


    int64_t granulepos = 0;

    for(;;) {
        if(feof(fp)) {
            printf("end of file reached\n");
            break;
        }

        while(ogg_sync_pageout(&oy, &og) == 1) {
            int err = ogg_stream_pagein(&os, &og);
            if(err) {
                fprintf(stderr, "error: ogg stream error\n");
                continue;
            }

            while(err = ogg_stream_packetout(&os, &op)) {
                if(err == -1) {
                    fprintf(stderr, "warning: gap in stream\n");
                    continue;
                }

                const unsigned char *data = op.packet;
                opus_int32 len = op.bytes;
                opus_int32 packet_offset;
                opus_int16 size[48];
                unsigned char toc;
                ogg_packet opo;


                for(int s=0; s<header->nb_streams; s++) {
                    int self_delimited = s != header->nb_streams-1;
                    int fcount = opus_packet_parse_impl(data, len, self_delimited, 
                        &toc, NULL, size, NULL, &packet_offset);

                    opo.packet = (unsigned char*)malloc(packet_offset);
                    opo.bytes = packet_offset;
                    opo.b_o_s = 0;
                    opo.e_o_s = op.e_o_s;
                    opo.granulepos = op.granulepos;
                    opo.packetno = op.packetno;

                    if(self_delimited) {
                        opo.packet[0] = toc;
                        if(opo.packet[1] < 252) {
                            memcpy(opo.packet+1, data+2, packet_offset-2);
                            opo.bytes = packet_offset-1;
                        } else {
                            memcpy(opo.packet+1, data+3, packet_offset-3);
                            opo.bytes = packet_offset-2;
                        }
                    } else {
                        memcpy(opo.packet, data, packet_offset);
                    }

                    file_writer_input(file_writers[s], &opo);
                    
                    if(op.granulepos >= 0) {
                        file_writer_update_granulepos(fw, op.granulepos);
                    }

                    data += packet_offset;
                    len -= packet_offset;

                    free(opo.packet);
                }

                //printf("packet %d %d %d %d\n", op.granulepos, op.bytes, op.packetno,
                //    valid);
                if(op.granulepos > 0) {
                    granulepos = op.granulepos;
                    
                }

            }
        }
        
        char *buf = ogg_sync_buffer(&oy, 4096);
        size_t n = fread(buf, 1, 4096, fp);
        int err = ogg_sync_wrote(&oy, n);
        if(err) {
            fprintf(stderr, "error: ogg sync error\n");
        }
    }

    printf("Closing file writers\n");
    for(int s=0; s<header->nb_streams; s++) {
        file_writer_close(file_writers[s]);
    }


  
  cleanup:
    if(fp) fclose(fp);
    if(header) free(header);
    //ogg_sync_destroy(&oy);
    return status;
}

