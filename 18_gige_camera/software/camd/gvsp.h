/*
 * gvsp.h — GVSP stream channel 0: leader / payload / trailer packets.
 */
#ifndef GVSP_H
#define GVSP_H

#include <pthread.h>
#include <stdint.h>

#include "camera.h"
#include "source.h"

struct streamer {
    struct camera       *cam;
    struct frame_source *src;
    pthread_t            thread;
    int                  fd;
    int                  stop;      /* protected by cam->lock */
    uint16_t             block_id;
    int                  verbose;
};

int  streamer_start(struct streamer *s, struct camera *cam, struct frame_source *src,
                    int verbose);
void streamer_stop(struct streamer *s);

#endif /* GVSP_H */
