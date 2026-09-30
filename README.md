# lcd-smartie-xygraph

**Description**:
- A side-scrolling bar graph that displays a percentage value over time.

**Format**:
- $dll(XYGraph,[deque#(for multiple instances)],[percentage value],[graph length]/[bar width(1-3)])

**Caveats**: 
- Only one bar width can be used per screen due to hardware limitations (custom character storage). 
- Only the final instance of XYGraph.dll needs bar width to be specified - all other instances on that screen will match. If bar width is not specified, it defaults to 3 (5 pixels wide).
