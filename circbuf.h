#ifndef __circbuf_h_
#define __circbuf_h_

#include <stdint.h>

typedef struct {
    void *buffer;
    int32_t length;
    int32_t wr;
    int32_t rd;
    volatile int32_t fill;
} circbuf_t;

circbuf_t *circbuf_new(size_t length);

int32_t circbuf_get_space(circbuf_t *buf);
int32_t circbuf_get_available(circbuf_t *buf);
int32_t circbuf_write(circbuf_t *buf, void *data, int32_t length);
int32_t circbuf_read(circbuf_t *buf, void *data, int32_t length);

#endif // __circbuf_h_

