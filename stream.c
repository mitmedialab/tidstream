#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "stream.h"

int n_channels;

shout_t *stream_setup(const char *host, int port, const char *password, 
 const char *mount) {
    shout_t *shout;

    /* shoutcast initialization */
    shout_init();
    shout = shout_new();
    if(!shout) {
        fprintf(stderr, "error creating shoutcast client\n");
        return NULL;
    }

    shout_set_host(shout, host);
    shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);
    shout_set_port(shout, port);
    shout_set_password(shout, password);
    shout_set_mount(shout, mount);
    shout_set_user(shout, "source");
    shout_set_format(shout, SHOUT_FORMAT_OGG);
    if(shout_open(shout) != SHOUTERR_SUCCESS) {
        fprintf(stderr, "shout: error connecting: %s\n", shout_get_error(shout));
        shout_free(shout);
        return NULL;
    }

    fprintf(stderr, "connected to http://%s:%d/%s\n", host, port, mount);

  
    return shout;
}


