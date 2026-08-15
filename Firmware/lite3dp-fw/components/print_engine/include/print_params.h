#pragma once

#include "profile_store.h"
#include "slicer_detect.h"

typedef struct {
    print_profile_t profile;
    slicer_type_t   slicer;
    char            folder_path[128];
    char            folder_name[64];
    int             total_layers;
} print_job_t;
