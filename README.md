# lcd-smartie-xygraph

# Description:
A side-scrolling bar graph that tracks a percentage value over time.

# Format:
$dll(XYGraph,[deque#(one for each instance)],[percentage value],[graph length]/[bar width(1-3)])

# Clarification:
- (deque#): Each instance of XYGraph.dll requires its own deque (max: 99).
- [bar width] = 1 ⟶ 1 pixel wide, [bar width] = 2 ⟶ 3 pixels wide, [bar width] = 3 ⟶ 5 pixels wide

# Caveats: 
- Only one bar width can be used per screen due to hardware limitations on custom character storage.
- Bar width specification is only required on the final instance of XYGraph.dll - all other instances on that screen will match. If no bar width is specified, it defaults to 3 (5 pixels wide).
