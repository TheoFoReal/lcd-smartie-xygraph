// XYGraph.cpp
// LCD Smartie plugin: bar graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc /O2 XYGraph.cpp /Fe:XYGraph.dll
//
// Usage:
//   $dll(XYGraph,1,<percentage>,<width>)
//
//   width = how many samples (bars) to display. Up to the width of
//           your LCD row. 16 for a 2x16, 20 for a 4x20, etc.
//
// How it works:
//   The 8 custom-character slots are used for the 8 possible bar
//   heights, not for individual time slots. Slot N holds a bar that
//   is N rows tall (N = 1..8). Each sample is then rendered by
//   emitting the $Chr() code for whichever height it needs, so the
//   same custom character is reused many times across the row.
//
//   A 0% sample is rendered as a plain space, since we have no spare
//   slot for a "0 rows filled" character.
//
// Custom-char slot mapping (LCD Smartie standard):
//   slot 1 -> $Chr(176)   slot 5 -> $Chr(133)
//   slot 2 -> $Chr(158)   slot 6 -> $Chr(134)
//   slot 3 -> $Chr(131)   slot 7 -> $Chr(135)
//   slot 4 -> $Chr(132)   slot 8 -> $Chr(136)

#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int MAX_HISTORY   = 128;  // samples kept in memory
static const int GRAPH_HEIGHT  = 8;    // pixels per character cell
static const int DEFAULT_WIDTH = 16;

// $Chr() code for each of the 8 custom-char slots
static const int CHR_CODES[8] = { 176, 158, 131, 132, 133, 134, 135, 136 };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static std::deque<int> g_history;

// ---------------------------------------------------------------------------
// Build a 5x8 character whose bottom `rows_filled` rows are on.
// rows_filled ranges 0..8. Row 0 is the top of the cell, row 7 bottom.
// ---------------------------------------------------------------------------
static std::vector<unsigned char> make_bar_char(int rows_filled)
{
    std::vector<unsigned char> ch(GRAPH_HEIGHT, 0);
    if (rows_filled < 0)             rows_filled = 0;
    if (rows_filled > GRAPH_HEIGHT)  rows_filled = GRAPH_HEIGHT;

    for (int i = 0; i < rows_filled; ++i) {
        ch[GRAPH_HEIGHT - 1 - i] = 0x1F;   // all 5 pixels on
    }
    return ch;
}

// ---------------------------------------------------------------------------
// Map a percentage to one of 8 height levels (1..8).
// Returns 0 to indicate "blank" (use a space).
// ---------------------------------------------------------------------------
static int percent_to_level(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    // 0% -> 0, 100% -> 8, rounded to nearest.
    int level = (pct * GRAPH_HEIGHT + 50) / 100;
    if (level < 0) level = 0;
    if (level > GRAPH_HEIGHT) level = GRAPH_HEIGHT;

    // Any nonzero reading shows at least one pixel, so the bar doesn't
    // disappear on values like 1-6%.
    if (level == 0 && pct > 0) level = 1;

    return level;
}

// ---------------------------------------------------------------------------
// Build the output string.
// ---------------------------------------------------------------------------
static std::string build_graph_string(int width)
{
    if (width < 1)            width = 1;
    if (width > MAX_HISTORY)  width = MAX_HISTORY;

    // Ensure enough history
    while ((int)g_history.size() < width) {
        g_history.push_front(0);
    }

    int start = (int)g_history.size() - width;

    std::ostringstream oss;

    // --- Define the 8 custom characters, one per height level 1..8 -------
    // These are the same definitions every call; they don't depend on
    // the samples at all. We still emit them each time so the display's
    // CGRAM is guaranteed to be correct even if something else in the
    // same screen has clobbered it.
    for (int level = 1; level <= GRAPH_HEIGHT; ++level) {
        std::vector<unsigned char> ch = make_bar_char(level);
        oss << "$CustomChar(" << level;
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }

    // --- Emit one character per sample -----------------------------------
    // A 0% sample becomes a space; everything else becomes the $Chr()
    // code for its height level.
    for (int i = 0; i < width; ++i) {
        int pct   = g_history[start + i];
        int level = percent_to_level(pct);

        if (level == 0) {
            oss << " ";
        } else {
            oss << "$Chr(" << CHR_CODES[level - 1] << ")";
        }
    }

    return oss.str();
}

// ---------------------------------------------------------------------------
// Plugin entry point
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) char* __stdcall function1(char* param1, char* param2)
{
    static std::string result;

    int pct   = 0;
    int width = DEFAULT_WIDTH;

    if (param1 && *param1) pct   = atoi(param1);
    if (param2 && *param2) width = atoi(param2);

    if (pct < 0)             pct   = 0;
    if (pct > 100)           pct   = 100;
    if (width < 1)           width = 1;
    if (width > MAX_HISTORY) width = MAX_HISTORY;

    g_history.push_back(pct);
    while ((int)g_history.size() > MAX_HISTORY) {
        g_history.pop_front();
    }

    result = build_graph_string(width);
    return const_cast<char*>(result.c_str());
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) void __stdcall SmartieInit()
{
    g_history.clear();
    for (int i = 0; i < DEFAULT_WIDTH; ++i) {
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
