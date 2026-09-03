/* Loads the audio chain in the requested mode and keeps it alive so the
   test battery can measure what actually comes out. */
#include "../src/audio.h"
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    bvp_config c;
    bvp_config_defaults(&c);
    c.dolby = true;
    c.dolby_mode = argc > 1 ? atoi(argv[1]) : 0;
    bvp_audio_set_filter_dir("filters");
    char *err = NULL;
    if (!bvp_audio_apply(&c, &err))
        return 1;
    sleep(30);
    bvp_audio_stop();
    return 0;
}
