#include "layer_manager.h"
#include <math.h>

int layer_image_index(int layer_num, float layer_height)
{
    /* Source images are always at 25µm (0.025mm) resolution.
     * Multiply layer_num by the ratio to skip images. */
    if (layer_height <= 0.03f) {
        return layer_num;       /* 25µm: 1:1 */
    } else if (layer_height <= 0.06f) {
        return layer_num * 2;   /* 50µm: skip every other */
    } else {
        return layer_num * 4;   /* 100µm: skip 3 of 4 */
    }
}

float layer_exposure_time(int layer_num, int bottom_layers, int transition_layers,
                          float bottom_exposure, float normal_exposure)
{
    if (layer_num < bottom_layers) {
        return bottom_exposure;
    }

    int trans_index = layer_num - bottom_layers;
    if (trans_index < transition_layers && transition_layers > 0) {
        /* Linear interpolation from bottom to normal exposure */
        float t = (float)(trans_index + 1) / (float)(transition_layers + 1);
        return bottom_exposure - t * (bottom_exposure - normal_exposure);
    }

    return normal_exposure;
}

float layer_lift_height(int layer_num, int bottom_layers, int transition_layers,
                        float initial_height, float normal_height)
{
    if (layer_num < bottom_layers + transition_layers) {
        return initial_height;
    }
    return normal_height;
}

float layer_lift_speed(int layer_num, int bottom_layers, int transition_layers,
                       float initial_speed, float normal_speed)
{
    if (layer_num < bottom_layers + transition_layers) {
        return initial_speed;
    }
    return normal_speed;
}
