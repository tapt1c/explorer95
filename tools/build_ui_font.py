"""Maintenance tool: derive the OFL font used by ANSI Windows 95.

Requires fontTools only when regenerating this committed resource.
Input: an unmodified LiberationSans-Regular.ttf (Liberation Fonts, SIL OFL 1.1).
The renamed derivative uses a Windows symbol cmap: GDI maps each CP1251 byte
through U+F000, without requiring a Russian system code-page installation.
The original Unicode mappings remain available to optional NT display calls.
"""
import sys
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import CmapSubtable

font = TTFont(sys.argv[1], recalcTimestamp=False)
cmap = font.getBestCmap().copy()
points = set(range(128))
for byte in range(128,256):
    try:
        points.add(ord(bytes([byte]).decode('cp1251')))
    except UnicodeDecodeError:
        pass
options = subset.Options()
options.name_IDs = ['*']
options.name_languages = ['*']
subsetter = subset.Subsetter(options=options)
subsetter.populate(unicodes=points)
subsetter.subset(font)
for name in font['name'].names:
    replacement = {1:'Explorer95 UI', 2:'Regular', 3:'Explorer95 UI CP1251 1.0',
                   4:'Explorer95 UI', 6:'Explorer95UI', 16:'Explorer95 UI',17:'Regular'}.get(name.nameID)
    if replacement:
        name.string = replacement.encode(name.getEncoding())
symbol = CmapSubtable.newSubtable(4)
symbol.platformID, symbol.platEncID, symbol.language = 3,0,0
symbol.cmap = {point:glyph for point,glyph in cmap.items() if point in points}
for byte in range(256):
    try:
        point = ord(bytes([byte]).decode('cp1251'))
    except UnicodeDecodeError:
        point = 0xfffd
    glyph = cmap.get(point)
    if glyph:
        symbol.cmap[byte] = glyph
        symbol.cmap[0xf000+byte] = glyph
font['cmap'].tables.append(symbol)
font['cmap'].tables.sort(key=lambda t:(t.platformID,t.platEncID,t.language))
font['OS/2'].version = 1  # TrueType table layout understood by Windows 95
font['OS/2'].ulCodePageRange1 = 1<<31
font['OS/2'].ulCodePageRange2 = 0
font.save(sys.argv[2])
