// XYGraph.cpp
// LCD Smartie plugin: bar graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc /O2 XYGraph.cpp /Fe:XYGraph.dll
//
// Usage:
//   $dll(XYGraph,1,<percentage>,<graph_width>)
//
// Custom-char slot mapping (LCD Smartie standard):
//   slot 1 -> $Chr(176)
//   slot 2 -> $Chr(158)
//   slot 3 -> $Chr(131)
//   slot 4 -> $Chr(132)
//   slot 5 -> $Chr(133)
//   slot 6 -> $Chr(134)
//   slot 7 -> $Chr(135)
//   slot 8 -> $Chr(136)

#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>

static const int MAX_HISTORY   = 64;
static const int GRAPH_HEIGHT  = 8;
static const int MAX_WIDTH     = 8;
static const int DEFAULT_WIDTH = 8;

// Correct LCD Smartie $Chr() codes for custom-char slots 1..8
static const int CHR_CODES[8] = { 176, 158, 131, 132, 133, 134, 135, 136 };

static std::deque<int> g_history;

static std::vector<unsigned char> make_bar_char(int pct)
{
    std::vector<unsigned char> ch(GRAPH_HEIGHT, 0);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int filled = (pct * GRAPH_HEIGHT + 50) / 100;
    if (filled < 0) filled = 0;
    if (filled > GRAPH_HEIGHT) filled = GRAPH_HEIGHT;

    for (int i = 0; i < filled; ++i) {
        ch[GRAPH_HEIGHT - 1 - i] = 0x1F;
    }
    return ch;
}

static std::string build_graph_string(int width)
{
    if (width < 1)         width = 1;
    if (width > MAX_WIDTH) width = MAX_WIDTH;

    while ((int)g_history.size() < width) {
        g_history.push_front(0);
    }

    std::vector<int> samples;
    for (int i = (int)g_history.size() - width; i < (int)g_history.size(); ++i) {
        samples.push_back(g_history[i]);
    }

    std::ostringstream oss;

    // --- Define ALL 8 custom-char slots ---------------------------------
    // Slots 1..width hold the actual bars. Slots width+1..8 are blank.
    // Because we will emit a $Chr() for every slot below, LCD Smartie
    // will forward all these definitions to the display, clearing any
    // stale bars from a previous wider render.
    for (int slot = 1; slot <= MAX_WIDTH; ++slot) {
        std::vector<unsigned char> ch;
        if (slot <= width) {
            ch = make_bar_char(samples[slot - 1]);
        } else {
            ch.assign(GRAPH_HEIGHT, 0);
        }

        oss << "$CustomChar(" << slot;
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }

    // --- Emit a $Chr() for EVERY slot 1..8 ------------------------------
    // Visible bars for slots 1..width, then spaces to push the blank
    // slots out of the visible area.
    for (int slot = 1; slot <= MAX_WIDTH; ++slot) {
        if (slot <= width) {
            oss << "$Chr(" << CHR_CODES[slot - 1] << ")";
        } else {
            oss << " ";   // pad with spaces so blank slots don't show
        }
    }

    return oss.str();
}

extern "C" __declspec(dllexport) char* __stdcall function1(char* param1, char* param2)
{
    static std::string result;

    int pct   = 0;
    int width = DEFAULT_WIDTH;

    if (param1 && *param1) pct = atoi(param1);
    if (param2 && *param2) width = atoi(param2);

    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (width < 1) width = 1;
    if (width > MAX_WIDTH) width = MAX_WIDTH;

    g_history.push_back(pct);
    while ((int)g_history.size() > MAX_HISTORY) {
        g_history.pop_front();
    }

    result = build_graph_string(width);
    return const_cast<char*>(result.c_str());
}

extern "C" __declspec(dllexport) void __stdcall SmartieInit()
{
    g_history.clear();
    for (int i = 0; i < MAX_WIDTH; ++i) {
        g_history.push_back(0);
    }
}

extern "C" __declspec(dllexport) void __stdcall SmartieFini()
{
    g_history.clear();
}

extern "C" __declspec(dllexport) int __stdcall GetMinRefreshInterval()
{
    return 500;
}
