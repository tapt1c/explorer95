"""Catalogue checks independent of the UI implementation and active locale."""
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('generate_translations',ROOT/'tools/generate_translations.py')
GENERATOR=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)

class LocalizationTests(unittest.TestCase):
    def test_catalogue_encoding_and_formats(self):
        catalogue=json.loads((ROOT/'resources/translations/ru.json').read_text(encoding='utf-8'))
        formats=re.compile(r'%(?:[-+ #0]*\d*(?:\.\d+)?[hlI]*[diuoxXcsSp]|%)')
        for source,translation in catalogue.items():
            with self.subTest(source=source):
                self.assertTrue(source and translation)
                self.assertNotIn('\0',source+translation)
                translation.encode('cp1251')
                self.assertEqual(formats.findall(source),formats.findall(translation))
    def test_generator_is_deterministic_and_keeps_shortcuts(self):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)/'strings.h'
            GENERATOR.generate(ROOT/'resources/translations/ru.json',target)
            first=target.read_bytes(); stamp=target.stat().st_mtime_ns
            GENERATOR.generate(ROOT/'resources/translations/ru.json',target)
            self.assertEqual(first,target.read_bytes())
            self.assertEqual(stamp,target.stat().st_mtime_ns)
            self.assertIn(b'\\tCtrl+',first)
            self.assertNotIn('Пуск'.encode('utf-8'),first) # portable C universal escapes
    def test_font_is_renamed_and_has_symbol_and_unicode_cmaps(self):
        # sfnt header/table directory; no fontTools dependency in CI.
        import struct
        data=(ROOT/'resources/Explorer95UI.ttf').read_bytes()
        self.assertEqual(data[:4],b'\0\1\0\0')
        tables={}
        for i in range(struct.unpack_from('>H',data,4)[0]):
            name,_,offset,length=struct.unpack_from('>4sIII',data,12+i*16)
            tables[name]=data[offset:offset+length]
        self.assertLessEqual(struct.unpack_from('>H',tables[b'OS/2'],0)[0],1)
        cmap=tables[b'cmap']; mappings=[]
        for i in range(struct.unpack_from('>H',cmap,2)[0]):
            platform,encoding,offset=struct.unpack_from('>HHI',cmap,4+i*8)
            mappings.append((platform,encoding,struct.unpack_from('>H',cmap,offset)[0]))
        self.assertIn((3,0,4),mappings)
        self.assertIn((3,1,4),mappings)
        names=tables[b'name']; records=[]
        _,count,strings=struct.unpack_from('>HHH',names,0)
        for i in range(count):
            platform,encoding,language,name_id,length,offset=struct.unpack_from('>6H',names,6+i*12)
            records.append((platform,encoding,name_id,names[strings+offset:strings+offset+length]))
        family='Explorer95 UI'.encode('utf-16-be')
        # Modern renderers accept Unicode-only names for symbol cmaps. Win95
        # needs matching Microsoft Symbol names as well as Unicode names.
        self.assertIn((3,0,1,family),records)
        self.assertIn((3,1,1,family),records)
        self.assertIn(b'SIL OPEN FONT LICENSE',(ROOT/'resources/LiberationSans-LICENSE.txt').read_bytes())

    def test_win95_font_has_only_the_legacy_symbol_character_range(self):
        import struct
        data=(ROOT/'resources/Explorer95UI95.ttf').read_bytes()
        tables={}
        for i in range(struct.unpack_from('>H',data,4)[0]):
            name,_,offset,length=struct.unpack_from('>4sIII',data,12+i*16)
            tables[name]=data[offset:offset+length]
        cmap=tables[b'cmap']
        self.assertEqual(struct.unpack_from('>H',cmap,2)[0],1)
        platform,encoding,offset=struct.unpack_from('>HHI',cmap,4)
        self.assertEqual((platform,encoding),(3,0))
        sub=cmap[offset:]; self.assertEqual(struct.unpack_from('>H',sub,0)[0],4)
        segments=struct.unpack_from('>H',sub,6)[0]//2
        points={}
        for i in range(segments):
            end=struct.unpack_from('>H',sub,14+2*i)[0]
            start=struct.unpack_from('>H',sub,16+2*segments+2*i)[0]
            delta=struct.unpack_from('>h',sub,16+4*segments+2*i)[0]
            position=16+6*segments+2*i
            span=struct.unpack_from('>H',sub,position)[0]
            for code in range(start,min(end,0xfffe)+1):
                glyph=struct.unpack_from('>H',sub,position+span+2*(code-start))[0] if span else code
                if glyph: glyph=(glyph+delta)&0xffff
                if glyph: points[code]=glyph
        self.assertTrue(points)
        self.assertTrue(all(0xf020<=code<=0xf0ff for code in points))
        for byte in b' A'+bytes([0xcf,0xf3,0xf1,0xea]):
            self.assertIn(0xf000+byte,points)
        os2=tables[b'OS/2']
        self.assertLessEqual(struct.unpack_from('>H',os2,0)[0],1)
        self.assertEqual(struct.unpack_from('>HH',os2,64),(min(points),max(points)))
        self.assertEqual(struct.unpack_from('>II',os2,78),(1<<31,0))
        self.assertIn('Explorer95 UI 95'.encode('utf-16-be'),tables[b'name'])

if __name__=='__main__': unittest.main()
