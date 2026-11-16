#ifndef CUCUT_FLOOD_HPP
#define CUCUT_FLOOD_HPP

#include "raster.hpp"

/** @brief Flood-fill traversal action for a neighborhood boundary pattern. */
struct FloodFillJobs
{
    PixelLocation next;
    int16_t count;
    PixelLocation queue[4];
};

/** @brief Four-neighborhood flood-fill actions indexed by boundary mask. */
extern const FloodFillJobs flood4[256];

/** @brief Eight-neighborhood flood-fill actions indexed by boundary mask. */
extern const FloodFillJobs flood8[256];

#endif // CUCUT_FLOOD_HPP
