#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef __APPLE__
#include <mach/mach.h>
#else
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include "circbuf.h"

#define bufdebug(...) fprintf(stderr, __VA_ARGS__)

#ifdef __APPLE__
circbuf_t *circbuf_new(size_t length) {
    circbuf_t *buf = (circbuf_t*)malloc(sizeof(circbuf_t));
    if(!buf) return NULL;

    size_t page_size = getpagesize();
    size_t buf_size = length + (page_size - 1);
    buf_size -= buf_size & (page_size - 1);

    bufdebug("page size: %d\n", buf_size);
    bufdebug("buffer size: %d\n", buf_size);

    int retries = 3;

    for(;;) {
        vm_address_t buffer_address;
        kern_return_t result = vm_allocate(mach_task_self(),
            &buffer_address, buf_size * 2, VM_FLAGS_ANYWHERE);

        if(result != ERR_SUCCESS) {
            if(retries-- == 0) {
                fprintf(stderr, "failed to allocate vm\n");
                return NULL;
            }
            continue;
        }

        result = vm_deallocate(mach_task_self(), buffer_address + buf_size,
            buf_size);
        if(result != ERR_SUCCESS) {
            if(retries-- == 0) {
                fprintf(stderr, "failed to deallocate second half\n");
                return NULL;
            }
            vm_deallocate(mach_task_self(), buffer_address, buf_size);
            continue;
        }

        vm_address_t virtual_address = buffer_address + buf_size;
        vm_prot_t cur_prot, max_prot;
        result = vm_remap(mach_task_self(), &virtual_address, buf_size,
            0, 0, mach_task_self(), buffer_address, 0, &cur_prot, &max_prot,
            VM_INHERIT_DEFAULT);
        if(result != ERR_SUCCESS) {
            if(retries-- == 0) {
                fprintf(stderr, "failed to remap second half\n");
                return NULL;
            }
            vm_deallocate(mach_task_self(), buffer_address, buf_size);
            continue;
        }

        if(virtual_address != buffer_address + buf_size) {
            if(retries-- == 0) {
                fprintf(stderr, "couldn't remap contiguous\n");
                return NULL;
            }
            vm_deallocate(mach_task_self(), virtual_address, buf_size);
            vm_deallocate(mach_task_self(), buffer_address, buf_size);
            continue;
        }
    
        buf->buffer = (void*)buffer_address;
        buf->length = buf_size;
        buf->wr = 0;
        buf->rd = 0;
        buf->fill = 0;

        return buf;
    }
    return NULL;
}
#else
circbuf_t *circbuf_new(size_t length) {
    circbuf_t *buf = (circbuf_t*)malloc(sizeof(circbuf_t));
    if(!buf) return NULL;

    size_t page_size = getpagesize();
    size_t buf_size = length + (page_size - 1);
    buf_size -= buf_size & (page_size - 1);

    bufdebug("page size: %d\n", buf_size);
    bufdebug("buffer size: %d\n", buf_size);

    size_t guard_size = page_size;

    uint8_t *pmem = (uint8_t*)mmap(0, 2*buf_size + 2*guard_size, PROT_NONE,
        MAP_ANON | MAP_PRIVATE, -1, 0);
    if(pmem == (uint8_t*)MAP_FAILED) {
        free(buf);
        bufdebug("circbuf: mmap failed\n");
        return NULL;
    }

    uint8_t *plower = pmem + guard_size;
    uint8_t *pupper = plower + buf_size;

    if(munmap(plower, 2*buf_size) < 0) {
        bufdebug("circbuf: munmap failed\n");
        // TODO: cleanup
        return NULL;
    }

    int shm_id;
    if((shm_id = shmget(IPC_PRIVATE, buf_size, IPC_CREAT | 0700)) < 0) {
        bufdebug("circbuf: shmget failed\n");
        // TODO: cleanup
        return NULL;
    }

    if( plower != shmat(shm_id, plower, 0) ||
        pupper != shmat(shm_id, pupper, 0)) {
        bufdebug("circbuf: shmat failed\n");
        perror("shmat failed");
        // TODO: cleanup
        return NULL;
    }
    
    buf->buffer = (void*)plower;
    buf->length = buf_size;
    buf->wr = 0;
    buf->rd = 0;
    buf->fill = 0;

    return buf;
}
#endif

int32_t circbuf_get_space(circbuf_t *buf) {
    return buf->length - buf->fill;
}

int32_t circbuf_get_available(circbuf_t *buf) {
    return buf->fill;
}

int32_t circbuf_write(circbuf_t *buf, void *data, int32_t length) {
    int32_t available = circbuf_get_space(buf);
    if(length > available) length = available;
    void *wrptr = &((uint8_t*)buf->buffer)[buf->wr];
    memcpy(wrptr, data, length);
    buf->fill += length;
    buf->wr += length;
    if(buf->wr >= buf->length) buf->wr -= buf->length;
    return length;
}

int32_t circbuf_read(circbuf_t *buf, void *data, int32_t length) {
    int32_t available = circbuf_get_available(buf);
    if(available < length) length = available;
    void *rdptr = ((uint8_t *)buf->buffer) + buf->rd;
    memcpy(data, rdptr, length);
    buf->rd += length;
    if(buf->rd >= buf->length) buf->rd -= buf->length;
    buf->fill -= length;
    return length;
}



