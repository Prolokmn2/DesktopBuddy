// A synthetic page, drawn as pixels, read back by src/vision/layout.cpp.
// Not a unit test framework: it prints what the reader saw so the output can be
// compared with the drawing by eye.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/vision/layout.h"

namespace
{
const int kW = 480;
const int kH = 260;

void Rect(std::vector<unsigned char>& img, int x, int y, int w, int h, unsigned char value)
{
    for (int dy = 0; dy < h; ++dy)
    {
        for (int dx = 0; dx < w; ++dx)
        {
            const int px = x + dx;
            const int py = y + dy;
            if (px < 0 || py < 0 || px >= kW || py >= kH)
            {
                continue;
            }
            const size_t at = (static_cast<size_t>(py) * kW + px) * 4;
            img[at] = value;
            img[at + 1] = value;
            img[at + 2] = value;
            img[at + 3] = 255;
        }
    }
}

// Text as blocks: each glyph is a 6x10 bar, which is enough for row and column
// projection to see what a line of text looks like.
void Word(std::vector<unsigned char>& img, int x, int y, int glyphs)
{
    for (int i = 0; i < glyphs; ++i)
    {
        Rect(img, x + i * 9, y, 6, 10, 20);
    }
}
}  // namespace

int main()
{
    std::vector<unsigned char> img(static_cast<size_t>(kW) * kH * 4, 255);

    // Two sentences, wrapping like a paragraph.
    Word(img, 24, 20, 8);
    Word(img, 100, 20, 6);
    Word(img, 160, 20, 4);
    Word(img, 24, 40, 10);
    Word(img, 120, 40, 7);

    // A table: three columns with wide, even gaps - the thing that used to read as
    // one long line with enormous spaces.
    Word(img, 24, 90, 3);
    Word(img, 200, 90, 3);
    Word(img, 380, 90, 3);
    Word(img, 24, 110, 3);
    Word(img, 200, 110, 3);
    Word(img, 380, 110, 3);

    // A black block: an empty region, not a space.
    Rect(img, 24, 160, 200, 60, 0);

    const std::string described = layout::Describe(img.data(), kW, kH);
    std::printf("%s", described.c_str());

    layout::ColumnMemory memory;
    std::printf("first sighting:  %s\n", memory.Remember({0, 200, 380}).c_str());
    std::printf("moved by 3px:    %s\n", memory.Remember({3, 197, 382}).c_str());
    std::printf("different shape: %s\n", memory.Remember({0, 120}).c_str());
    return 0;
}
