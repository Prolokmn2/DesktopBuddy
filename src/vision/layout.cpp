#include "layout.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace layout
{
namespace
{
struct Span
{
    int from = 0;
    int to = 0;  // exclusive
};

// One pass over the pixels: luminance, then per-row and per-column ink counts.
struct InkMap
{
    std::vector<int> rowInk;
    std::vector<unsigned char> mask;  // 1 where a pixel is ink
    int width = 0;
    int height = 0;
};

InkMap Build(const uint8_t* bgra, int width, int height, int threshold)
{
    InkMap map;
    map.width = width;
    map.height = height;
    map.rowInk.assign(static_cast<size_t>(height), 0);
    map.mask.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t at = (static_cast<size_t>(y) * width + x) * 4;
            // BGRA, but weight the channels the way the eye does anyway.
            const int luma = (bgra[at] * 29 + bgra[at + 1] * 150 + bgra[at + 2] * 77) >> 8;
            if (luma < threshold)
            {
                map.mask[static_cast<size_t>(y) * width + x] = 1;
                ++map.rowInk[static_cast<size_t>(y)];
            }
        }
    }
    return map;
}

// Runs of consecutive indexes where "has" is true, with runs shorter than minLen
// folded into their neighbour: that is what glues the dots of an i to its stem.
std::vector<Span> Runs(const std::vector<char>& has, int minLen)
{
    std::vector<Span> spans;
    int start = -1;
    for (size_t i = 0; i <= has.size(); ++i)
    {
        const bool inside = i < has.size() && has[i] != 0;
        if (inside && start < 0)
        {
            start = static_cast<int>(i);
        }
        else if (!inside && start >= 0)
        {
            spans.push_back(Span{start, static_cast<int>(i)});
            start = -1;
        }
    }

    if (minLen <= 1)
    {
        return spans;
    }

    std::vector<Span> merged;
    for (const Span& span : spans)
    {
        if (!merged.empty() && span.to - span.from < minLen &&
            span.from - merged.back().to <= 2)
        {
            merged.back().to = span.to;
            continue;
        }
        merged.push_back(span);
    }
    return merged;
}

std::string Join(const std::vector<int>& columns)
{
    std::string out;
    for (size_t i = 0; i < columns.size(); ++i)
    {
        if (i != 0)
        {
            out += ',';
        }
        out += std::to_string(columns[i]);
    }
    return out;
}
}  // namespace

std::string Describe(const uint8_t* bgra, int width, int height, const Options& options)
{
    if (!bgra || width <= 2 || height <= 2)
    {
        return std::string();
    }

    const InkMap map = Build(bgra, width, height, options.inkThreshold);

    std::vector<char> rowHas(static_cast<size_t>(height), 0);
    for (int y = 0; y < height; ++y)
    {
        // A single dark pixel on a row is noise, a hundred is a line of text.
        rowHas[static_cast<size_t>(y)] = map.rowInk[static_cast<size_t>(y)] > 1 ? 1 : 0;
    }

    const std::vector<Span> lines = Runs(rowHas, 3);
    std::string out;
    char buffer[256] = {};

    out += "size " + std::to_string(width) + "x" + std::to_string(height) + "\n";
    out += "lines " + std::to_string(lines.size()) + "\n";

    for (size_t index = 0; index < lines.size(); ++index)
    {
        const Span& line = lines[index];

        // Column projection inside this line only: what is a gap in one line is not
        // necessarily a gap in the next.
        std::vector<int> columnInk(static_cast<size_t>(width), 0);
        for (int y = line.from; y < line.to; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (map.mask[static_cast<size_t>(y) * width + x])
                {
                    ++columnInk[static_cast<size_t>(x)];
                }
            }
        }

        std::vector<char> colHas(static_cast<size_t>(width), 0);
        int maxInk = 1;
        for (int x = 0; x < width; ++x)
        {
            colHas[static_cast<size_t>(x)] = columnInk[static_cast<size_t>(x)] > 0 ? 1 : 0;
            maxInk = std::max(maxInk, columnInk[static_cast<size_t>(x)]);
        }
        const std::vector<Span> words = Runs(colHas, 2);

        // The usual space between words: the median gap. Anything much wider than that
        // is a column edge, not a space - which is what stops a table from reading as
        // one long sentence with huge spaces in it.
        std::vector<int> gaps;
        for (size_t w = 1; w < words.size(); ++w)
        {
            gaps.push_back(words[w].from - words[w - 1].to);
        }
        int typical = 0;
        if (!gaps.empty())
        {
            std::vector<int> sorted = gaps;
            std::sort(sorted.begin(), sorted.end());
            typical = sorted[sorted.size() / 2];
        }

        std::vector<int> columns{0};
        for (size_t w = 1; w < words.size(); ++w)
        {
            const int gap = words[w].from - words[w - 1].to;
            if (typical > 0 && gap > static_cast<int>(typical * options.columnGapFactor))
            {
                columns.push_back(words[w].from);
            }
        }

        std::snprintf(buffer, sizeof(buffer), "  y=%d..%d words=%d", line.from, line.to,
                      static_cast<int>(words.size()));
        out += buffer;
        if (columns.size() > 1)
        {
            out += " columns=" + Join(columns);
        }
        out += "\n";
    }

    // A block earns EMPTY_REGION when it is flat and dark: a black rectangle, a panel
    // that failed to render, the inside of a video. Plain background is not reported -
    // calling every white patch "empty" buries the answer in noise.
    int empties = 0;
    const int step = options.emptyBlock;
    for (int y = 0; y + step <= height; y += step)
    {
        int runStart = -1;  // first dark block of the run being collected
        for (int x = 0; x + step <= width; x += step)
        {
            int darkest = 255;
            int brightest = 0;
            for (int dy = 0; dy < step; ++dy)
            {
                for (int dx = 0; dx < step; ++dx)
                {
                    const size_t at = (static_cast<size_t>(y + dy) * width + (x + dx)) * 4;
                    const int luma =
                        (bgra[at] * 29 + bgra[at + 1] * 150 + bgra[at + 2] * 77) >> 8;
                    darkest = std::min(darkest, luma);
                    brightest = std::max(brightest, luma);
                }
            }

            // Flat (no detail) and dark (nothing legible) is a hole, not content.
            const bool hole = brightest - darkest < 12 && brightest < 60;
            if (hole && runStart < 0)
            {
                runStart = x;
            }
            else if (!hole && runStart >= 0)
            {
                ++empties;
                std::snprintf(buffer, sizeof(buffer),
                              "  EMPTY_REGION x=%d y=%d w=%d h=%d", runStart, y, x - runStart,
                              step);
                out += buffer;
                out += "\n";
                runStart = -1;
            }
        }
        if (runStart >= 0)
        {
            ++empties;
            std::snprintf(buffer, sizeof(buffer), "  EMPTY_REGION x=%d y=%d w=%d h=%d",
                          runStart, y, width - runStart, step);
            out += buffer;
            out += "\n";
        }
    }
    out += "empty_blocks " + std::to_string(empties) + "\n";

    return out;
}

std::string ColumnMemory::Remember(const std::vector<int>& columns)
{
    for (size_t i = 0; i < known_.size(); ++i)
    {
        const std::vector<int>& known = known_[i];
        if (known.size() != columns.size())
        {
            continue;
        }

        bool same = true;
        for (size_t c = 0; c < columns.size(); ++c)
        {
            if (std::abs(columns[c] - known[c]) > 8)
            {
                same = false;
                break;
            }
        }
        if (same)
        {
            return "TABLE " + std::to_string(i + 1);
        }
    }

    known_.push_back(columns);
    return "TABLE " + std::to_string(known_.size());
}
}  // namespace layout
