/* The services' state for a snapshot (platform/native/host.h): the timing
 * service's frame, waits, debt and generator, the pads, the video service's
 * display and its observer's memory and frame, and the save service's mark.
 * What binds the services to this run -- the program's callbacks, the
 * driver's fiber, the observer's reader, the sound host, the save's bytes and
 * observer -- stays as the run has it. */
#include <string.h>

#include "platform/native/host.h"
#include "platform/observe.h"

size_t pf_host_snapshot(const Platform *pf, uint8_t *out, size_t room)
{
    size_t video = vid_obs_snapshot(pf->video.observer, NULL, 0);
    size_t need = sizeof *pf + video;
    if (!out || room < need)
        return need;
    memcpy(out, pf, sizeof *pf);
    vid_obs_snapshot(pf->video.observer, out + sizeof *pf, video);
    return need;
}

bool pf_host_restore(Platform *pf, const uint8_t *in, size_t len)
{
    if (len < sizeof *pf || !vid_obs_restore(pf->video.observer, in + sizeof *pf, len - sizeof *pf))
        return false;
    Platform live = *pf;
    memcpy(pf, in, sizeof *pf);
    pf->timing.program = live.timing.program;
    pf->timing.driver = live.timing.driver;
    pf->video.observer = live.video.observer;
    pf->video.read = live.video.read;
    pf->video.write = live.video.write;
    pf->video.ctx = live.video.ctx;
    pf->audio = live.audio;
    pf->save.bytes = live.save.bytes;
    pf->save.ctx = live.save.ctx;
    pf->save.written = live.save.written;
    return true;
}
