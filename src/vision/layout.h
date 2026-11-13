// Turns pixels into a description, with no model involved.
//
// This is the fallback for when the page bridge cannot read a page: instead of asking
// an AI to look at a picture, project the ink. Rows become lines, columns become
// table columns, and a block that is uniformly near-black is reported as EMPTY_REGION
// rather than being mistaken for a wide gap between words.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace layout
{
struct Options
{
    // Pixels darker than this (0-255) count as ink.
    int inkThreshold = 150;
    // A vertical gap this much wider than the usual word gap starts a new column.
    float columnGapFactor = 2.5f;
    // A square this size whose brightest pixel is under inkThreshold is blank space,
    // not content.
    int emptyBlock = 24;
};

// BGRA8, top-down, stride is width * 4. Never throws on odd sizes: a zero width or
// height returns an empty description.
std::string Describe(const uint8_t* bgra, int width, int height, const Options& options = {});

// Remembers column layouts across calls so the same table is called TABLE 1 every
// time, even after the rows move. Columns match if every edge is within a few pixels.
class ColumnMemory
{
public:
    // Stable name for this set of columns: "TABLE 1", "TABLE 2", and a new one when
    // the shape is not one that was seen before.
    std::string Remember(const std::vector<int>& columns);

private:
    std::vector<std::vector<int>> known_;
};
}  // namespace layout
