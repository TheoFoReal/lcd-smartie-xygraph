# lcd-smartie-xygraph

**Description**:
-Tracks a percentage value over time using a side-scrolling bar graph. 

**Format**:
-$dll(XYGraph,[deque#(for multiple instances)],[percentage value],[graph length]/[bar width(1-3)])

**Caveats**: 
- Only one bar width can be used per screen due to hardware limitations (custom character storage). 
- Only the final instance of XYGraph.dll needs bar width to be specified - all other instances will match. If bar width is not specified, it defaults to 3 (5 pixels wide).
