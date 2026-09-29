// XYGraph.cpp
// LCD Smartie plugin: x/y graph tracking a percentage over time.
// Build with MSVC: cl /LD /EHsc XYGraph.cpp /Fe:XYGraph.dll
//
// Usage in LCD Smartie:
//   $dll(XYGraph,1,<percentage>,<graph_width>)
//   Example: $dll(XYGraph,1,75,16)  -> 16-character-wide graph
//
// The plugin stores the last N percentage values in a ring buffer and
// renders a line graph using $CustomChar definitions. Each column of the
// graph is one custom character containing the line segment for that
// time step.

#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <algorithm>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const int MAX_HISTORY = 64;   // maximum samples kept in memory
static const int GRAPH_HEIGHT = 8;   // vertical pixels per character cell

// ---------------------------------------------------------------------------
// Internal state (persists between calls)
// ---------------------------------------------------------------------------
static std::deque<int> g_history;    // percentage values (0-100)

// ---------------------------------------------------------------------------
// Helper: map a percentage to a row index (0 = bottom, 7 = top)
// ---------------------------------------------------------------------------
static int percent_to_row(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    // Map 0-100 to rows 7-0 (row 0 is top of the 8-pixel cell)
    return 7 - (pct * 7) / 100;
}

// ---------------------------------------------------------------------------
// Helper: build a single 5x8 custom character that draws a line from
// (prev_row) to (curr_row) within one character cell.
// Returns 8 bytes (one per row), each byte's lower 5 bits are the pixels.
// ---------------------------------------------------------------------------
static std::vector<unsigned char> make_line_char(int prev_row, int curr_row)
{
    std::vector<unsigned char> ch(8, 0);
    if (prev_row < 0) prev_row = 7;
    if (curr_row < 0) curr_row = 7;
    if (prev_row > 7) prev_row = 0;
    if (curr_row > 7) curr_row = 0;

    int r0 = (prev_row < curr_row) ? prev_row : curr_row;
    int r1 = (prev_row < curr_row) ? curr_row : prev_row;

    for (int row = r0; row <= r1; ++row) {
        // Simple line: set the middle pixel(s) on the rows between
        // the two endpoints. For a smoother look we set two pixels.
        ch[row] = 0x1F;  // all 5 pixels on – thick line segment
    }
    // Add endpoints
    ch[prev_row] |= 0x1F;
    ch[curr_row] |= 0x1F;
    return ch;
}

// ---------------------------------------------------------------------------
// Helper: build the complete $CustomChar() string for a graph of `width`
// columns. Uses characters 1..width (up to 8 custom chars available).
// ---------------------------------------------------------------------------
static std::string build_graph_string(int width)
{
    if (width < 1) width = 1;
    if (width > 8) width = 8;   // LCD Smartie supports up to 8 custom chars

    // Ensure we have enough history
    while ((int)g_history.size() < width) {
        g_history.push_front(0);
    }

    // Take the last `width` samples
    std::vector<int> samples;
    for (int i = (int)g_history.size() - width; i < (int)g_history.size(); ++i) {
        samples.push_back(g_history[i]);
    }

    // Build custom char definitions and the display string
    std::ostringstream oss;
    for (int i = 0; i < width; ++i) {
        int curr_pct = samples[i];
        int prev_pct = (i == 0) ? curr_pct : samples[i - 1];

        int prev_row = percent_to_row(prev_pct);
        int curr_row = percent_to_row(curr_pct);

        std::vector<unsigned char> ch = make_line_char(prev_row, curr_row);

        // Emit $CustomChar(n, b0, b1, ..., b7)
        oss << "$CustomChar(" << (i + 1);
        for (int b = 0; b < 8; ++b) {
            oss << "," << (int)ch[b];
        }
        oss << ")";
    }

    // Now emit the characters themselves using $Chr(176 + n)
    // Custom char 1 is code 176, char 2 is 177, etc.
    for (int i = 0; i < width; ++i) {
        oss << "$Chr(" << (176 + i) << ")";
    }

    return oss.str();
}

// ---------------------------------------------------------------------------
// Exported plugin functions
// ---------------------------------------------------------------------------

// Called by LCD Smartie: $dll(XYGraph,1,param1,param2)
//   param1 = current percentage (0-100)
//   param2 = graph width in characters (default 16)
extern "C" __declspec(dllexport) char* __stdcall function1(char* param1, char* param2)
{
    static std::string result;

    int pct = 0;
    int width = 16;

    if (param1 && *param1) {
        pct = atoi(param1);
    }
    if (param2 && *param2) {
        width = atoi(param2);
    }

    // Clamp
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (width < 1) width = 1;
    if (width > 8) width = 8;

    // Push new sample
    g_history.push_back(pct);
    while ((int)g_history.size() > MAX_HISTORY) {
        g_history.pop_front();
    }

    // Build and return the graph string
    result = build_graph_string(width);
    return const_cast<char*>(result.c_str());
}

// Optional: called when the plugin is first loaded
extern "C" __declspec(dllexport) void __stdcall SmartieInit()
{
    g_history.clear();
    // Seed with a few zeros so the graph doesn't start empty
    for (int i = 0; i < 8; ++i) {
        g_history.push_back(0);
    }
}

// Optional: called when the plugin is unloaded
extern "C" __declspec(dllexport) void __stdcall SmartieFini()
{
    g_history.clear();
}

// Optional: minimum refresh interval in milliseconds.
// Return 500 to update twice per second.
extern "C" __declspec(dllexport) int __stdcall GetMinRefreshInterval()
{
    return 500;
}
