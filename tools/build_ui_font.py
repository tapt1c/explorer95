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
from fontTools.ttLib.tables._n_a_m_e import makeName

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
# Windows 95 selects name records for the active Microsoft cmap encoding.
# The symbol cmap must have matching Microsoft Symbol (3,0) names, not only
# Unicode (3,1) names as modern font engines tolerate.
for name in list(font['name'].names):
    if name.platformID == 3 and name.platEncID == 1:
        font['name'].names.append(makeName(name.toUnicode(),name.nameID,3,0,name.langID))
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

# Win95's font mapper must see an unambiguous Microsoft Symbol font. A mixed
# Unicode/symbol font can register successfully but be mapped as another
# charset. Keep the NT font above for real Unicode control captions; the
# legacy resource contains only F000+byte mappings and matching symbol names.
if len(sys.argv)>3:
    classic=TTFont(sys.argv[2],recalcTimestamp=False)
    classic.getGlyphOrder()
    legacy_symbol=next(table for table in classic['cmap'].tables if table.platformID==3 and table.platEncID==0)
    legacy_symbol.cmap={point:glyph for point,glyph in legacy_symbol.cmap.items() if 0xf020<=point<=0xf0ff}
    classic['cmap'].tables=[legacy_symbol]
    for tag in ('GDEF','GPOS','GSUB'):
        if tag in classic: del classic[tag]
    classic['name'].names=[name for name in classic['name'].names if name.platformID==3 and name.platEncID==0]
    legacy_names={1:'Explorer95 UI 95',2:'Regular',3:'Explorer95 UI 95 CP1251 1.0',
                  4:'Explorer95 UI 95',6:'Explorer95UI95',16:'Explorer95 UI 95',17:'Regular'}
    for name in classic['name'].names:
        if name.nameID in legacy_names:
            name.string=legacy_names[name.nameID].encode(name.getEncoding())
    # Macintosh names help pre-Unicode TrueType tooling; all UI family names
    # are ASCII and remain independent of the installed Windows code page.
    for name_id,value in legacy_names.items():
        classic['name'].names.append(makeName(value,name_id,1,0,0))
    classic['OS/2'].ulUnicodeRange1=0
    classic['OS/2'].ulUnicodeRange2=1<<28  # Private Use Area, F020..F0FF
    classic['OS/2'].ulUnicodeRange3=classic['OS/2'].ulUnicodeRange4=0
    classic.save(sys.argv[3])
