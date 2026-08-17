#pragma once

#include "profile_store.h"
#include "slicer_detect.h"

typedef struct {
    print_profile_t profile;
    slicer_type_t   slicer;
    char            folder_path[128];
    char            folder_name[64];
    /* Layer files are named after the project, which need not match the
     * folder — see slicer_detect(). Do not index layers with folder_name. */
    char            layer_prefix[SLICER_PREFIX_MAX];
    int             layer_digits;
    int             total_layers;
} print_job_t;
