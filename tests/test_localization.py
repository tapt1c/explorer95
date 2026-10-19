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
        self.assertIn('Explorer95 UI'.encode('utf-16-be'),tables[b'name'])
        self.assertIn(b'SIL OPEN FONT LICENSE',(ROOT/'resources/LiberationSans-LICENSE.txt').read_bytes())

if __name__=='__main__': unittest.main()
