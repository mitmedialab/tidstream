#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <ogg/ogg.h>

#include "opus_header.h"
#include "opus_utils.h"
#include "file_writer.h"

#define readint(buf, base) ( (buf[base]) + (buf[base+1] << 8) + \
                             (buf[base+2] << 16) + (buf[base+3] << 24) );

void usage(char *exe) {
    fprintf(stderr, "usage: %s [options] infile.opus\n", exe);
    fprintf(stderr, "\t--file <string>\t Specify the input file.\n");
    fprintf(stderr, "\t--out <string>\t Specify the destination filename (default: input filename).\n");
    fprintf(stderr, "\t--chuck-size <int>\t Specify the chuck size in seconds (default: 3600).\n");
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
    char *filename_output     = NULL;
    char *filename_input      = NULL;
    int  chuck_size           = 3600;
    int c;
    int digit_optind = 0;

    while (1) {
      int this_option_optind = optind ? optind : 1;
      int option_index = 0;
      static struct option long_options[] = {
          {"out",         optional_argument, NULL,  0 },
          {"file",        required_argument, NULL,  1 },
          {"chunck-size", optional_argument, NULL,  2 },
          {0,             0,                 0,     0 }
        };
      c = getopt_long(argc, argv, "o:f:",long_options, &option_index);
      if (c == -1) break;

      switch (c) {
        case 0: filename_output = optarg;           break;
        case 1: filename_input  = optarg;           break;
        case 2: chuck_size      = atoi(optarg);     break;
        default: usage(argv[0]);
        }
    }

    if (filename_input == NULL){
      usage(argv[0]);
      return -1;
    }

    if (filename_output == NULL) filename_output = filename_input;

    printf("Input : %s\n",      filename_input);
    printf("Output : %s\n",     filename_output);
    printf("Chuck size : %d\n", chuck_size);

    FILE *fp = fopen(filename_input, "rb");
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

    printf("Reading file: %s\n", filename_input);

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

    printf("Creating file writer for %s\n", filename_output);
    OpusFileWriter* file_writers = (OpusFileWriter*)malloc(sizeof(OpusFileWriter));
    file_writer_init(file_writers, filename_output, header, comment_header, comment_length);
    file_writer_set_max_length(file_writers, header->input_sample_rate * chuck_size);

    int64_t granulepos = 0;

    for(;;) {
        if(feof(fp)) {
            printf("end of file reached\n");
            break;
        }

        while(ogg_sync_pageout(&oy, &og) == 1) {
            int err = ogg_stream_pagein(&os, &og);
            if(err) {
                printf( "error: ogg stream error\n");
                continue;
            }

            while(err = ogg_stream_packetout(&os, &op)) {
                if(err == -1) {
                    printf( "warning: gap in stream\n");
                    continue;
                }
                file_writer_input(file_writers, &op);
                file_writer_update_granulepos(file_writers, op.granulepos);
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
    file_writer_close(file_writers);



  cleanup:
    if(fp) fclose(fp);
    if(header) free(header);
    //ogg_sync_destroy(&oy);
    return status;
}
