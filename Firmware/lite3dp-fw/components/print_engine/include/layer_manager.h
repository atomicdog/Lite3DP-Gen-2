#pragma once

#include "esp_err.h"
#include "slicer_detect.h"

/**
 * Calculate the actual image index for a given print layer,
 * accounting for layer height vs source image resolution.
 *
 * Source images are always at 25µm resolution.
 * - At 25µm layer height: image_index = layer_num
 * - At 50µm layer height: image_index = layer_num * 2
 * - At 100µm layer height: image_index = layer_num * 4
 *
 * @param layer_num     Print layer number (0-indexed)
 * @param layer_height  Layer height in mm
 * @return              Image index for the slicer filename generator
 */
int layer_image_index(int layer_num, float layer_height);

/**
 * Convert a count of source images into the number of layers actually
 * printed at the given layer height — the inverse of the skip ratio
 * layer_image_index applies.
 *
 * Both must agree, or the layer loop indexes past the last image: 3151
 * source images printed at 0.1mm is 787 layers, not 3151. Taking the raw
 * file count as the layer count meant the job ran four times too long and
 * died at ~25% asking for an image that was never exported.
 *
 * @param image_count   Number of layer PNGs in the job folder
 * @param layer_height  Layer height in mm
 * @return              Number of printable layers (>= 0)
 */
int layer_count_for_height(int image_count, float layer_height);

/**
 * Calculate the exposure time for a given layer, handling
 * bottom layers, transition layers, and normal layers.
 *
 * @param layer_num         Current layer (0-indexed)
 * @param bottom_layers     Number of bottom layers
 * @param transition_layers Number of transition layers
 * @param bottom_exposure   Bottom layer exposure (seconds)
 * @param normal_exposure   Normal layer exposure (seconds)
 * @return                  Exposure time in seconds
 */
float layer_exposure_time(int layer_num, int bottom_layers, int transition_layers,
                          float bottom_exposure, float normal_exposure);

/**
 * Determine lift height for a given layer.
 * Bottom + transition layers use initial lift height; normal layers use standard.
 */
float layer_lift_height(int layer_num, int bottom_layers, int transition_layers,
                        float initial_height, float normal_height);

/**
 * Determine lift speed for a given layer.
 */
float layer_lift_speed(int layer_num, int bottom_layers, int transition_layers,
                       float initial_speed, float normal_speed);
