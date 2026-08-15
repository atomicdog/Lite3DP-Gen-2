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
