// XYGraph.cpp
// LCD Smartie plugin: bar graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc /O2 XYGraph.cpp /Fe:XYGraph.dll
//
// Usage in LCD Smartie:
//   $dll(XYGraph,1,<percentage>,<graph_width>)
//   Example: $dll(XYGraph,1,75,4)  -> 4-bar-wide graph
//
// The plugin stores the last N percentage values in a ring buffer and
// renders each sample as a vertical bar. Each column is one custom
// character whose bottom rows are filled in proportion to the value.
//
// IMPORTANT: this version defines ALL EIGHT custom-character slots on
// every call. Slots 1..width hold the bar for each sample; slots
// width+1..8 are explicitly defined as blank. This prevents the display
// from showing stale bars left over from a previous call that used a
// wider graph (HD44780 CGRAM is not cleared between renders).
//
// NOTE: LCD Smartie displays custom character N (defined with
//       $CustomChar(N, ...)) using $Chr(N-1). So slot 1 is shown with
//       $Chr(0), slot 2 with $Chr(1), and so on up to $Chr(7) for slot 8.

#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int MAX_HISTORY   = 64;   // maximum samples kept in memory
static const int GRAPH_HEIGHT  = 8;    // vertical pixels per character cell
static const int MAX_WIDTH     = 8;    // LCD Smartie has 8 custom char slots
static const int DEFAULT_WIDTH = 8;    // used when param2 is missing/empty

// ---------------------------------------------------------------------------
// Internal state (persists between calls)
// ---------------------------------------------------------------------------
static std::deque<int> g_history;      // percentage values (0-100)

// ---------------------------------------------------------------------------
// Helper: build a 5x8 custom character that draws a vertical bar filled
// from the bottom up. Row 0 is the top of the cell, row 7 is the bottom.
// ---------------------------------------------------------------------------
static std::vector<unsigned char> make_bar_char(int pct)
{
    std::vector<unsigned char> ch(GRAPH_HEIGHT, 0);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    // Number of rows to fill from the bottom: 0..8.
    // The +50 rounds to nearest so 50% lands on 4 rows, not 3.
    int filled = (pct * GRAPH_HEIGHT + 50) / 100;
    if (filled < 0) filled = 0;
    if (filled > GRAPH_HEIGHT) filled = GRAPH_HEIGHT;

    for (int i = 0; i < filled; ++i) {
        ch[GRAPH_HEIGHT - 1 - i] = 0x1F;  // all 5 pixels wide
    }
    return ch;
}

// ---------------------------------------------------------------------------
// Helper: build the complete $CustomChar()/$Chr() string for a bar graph of
// `width` columns. Always defines all MAX_WIDTH slots so unused slots are
// forced to blank.
// ---------------------------------------------------------------------------
static std::string build_graph_string(int width)
{
    if (width < 1)         width = 1;
    if (width > MAX_WIDTH) width = MAX_WIDTH;

    // Ensure we have enough history.
    while ((int)g_history.size() < width) {
        g_history.push_front(0);
    }

    // Take the last `width` samples.
    std::vector<int> samples;
    for (int i = (int)g_history.size() - width; i < (int)g_history.size(); ++i) {
        samples.push_back(g_history[i]);
    }

    std::ostringstream oss;

    // --- Define ALL 8 custom-char slots every call -----------------------
    for (int slot = 1; slot <= MAX_WIDTH; ++slot) {
        std::vector<unsigned char> ch;
        if (slot <= width) {
            ch = make_bar_char(samples[slot - 1]);
        } else {
            ch.assign(GRAPH_HEIGHT, 0);   // blank char for unused slots
        }

        oss << "$CustomChar(" << slot;
        for (int b = 0; b < GRAPH_HEIGHT; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }

    // --- Emit only `width` visible characters ---------------------------
    // Slot N is displayed with $Chr(N-1), so width=4 -> $Chr(0..3).
    for (int i = 0; i < width; ++i) {
        oss << "$Chr(" << i << ")";
    }

    return oss.str();
}

// ---------------------------------------------------------------------------
// Exported plugin function
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) char* __stdcall function1(char* param1, char* param2)
{
    static std::string result;

    int pct   = 0;
    int width = DEFAULT_WIDTH;

    if (param1 && *param1) {
        pct = atoi(param1);
    }
    if (param2 && *param2) {
        width = atoi(param2);
    }

    // Clamp
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (width < 1) width = 1;
    if (width > MAX_WIDTH) width = MAX_WIDTH;

    // Push new sample.
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
