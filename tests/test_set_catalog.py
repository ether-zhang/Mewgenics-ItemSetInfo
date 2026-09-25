"""Synthetic regression cases plus read-only installed-resource validation."""
import csv
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from catalog.build_catalog import (  # noqa: E402
    build_catalog, clean_text, generate_header, load_catalog, translations,
)
from catalog.game_archive import GameArchive  # noqa: E402
from catalog.gon import parse_gon  # noqa: E402


def localization(rows, fields=('KEY', 'en', 'zh-cn')):
    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=fields)
    writer.writeheader()
    for row in rows:
        writer.writerow(row)
    return output.getvalue()


def fixture():
    sets = '''
    Bronze {
        name BRONZE
        desc BRONZE_DESC
        pieces_required 3
        shield 4
        spd -1
        passives { TestPassive 1 }
    }
    Cloth {
        name CLOTH
        desc CLOTH_DESC
        pieces_required 2
        dex 2
    }
    Unused {
        name UNUSED
        desc UNUSED_DESC
        pieces_required 3
        str 1
    }
    '''
    items = {'test.gon': '''
        HybridHat {
            name HAT
            set [Bronze, Cloth]
        }
        WildMask {
            name MASK
            set [Cloth *]
        }
        Ordinary {
            name ORDINARY
        }
    '''}
    csv_text = localization([
        {'KEY': 'BRONZE', 'zh-cn': '青铜套装'},
        {'KEY': 'BRONZE_DESC', 'zh-cn': '[b]被攻击时[/b]获得 [img:shield]。'},
        {'KEY': 'CLOTH', 'zh-cn': '布套装'},
        {'KEY': 'CLOTH_DESC', 'zh-cn': ''},
        {'KEY': 'UNUSED', 'zh-cn': '未使用套装'},
        {'KEY': 'UNUSED_DESC', 'zh-cn': ''},
        {'KEY': 'HAT', 'zh-cn': '混合帽'},
        {'KEY': 'MASK', 'zh-cn': '万能面具'},
    ])
    return sets, items, translations(csv_text)


class CatalogTests(unittest.TestCase):
    def test_gon_arrays_comments_and_quoted_text(self):
        parsed = parse_gon('''// comment
            Thing {
                name "ONE TWO"
                set [First, /* inline */ Second
                    Third]
                effect { key "a\\nb" }
            }
        ''')
        self.assertEqual(parsed['Thing']['set'], ['First', 'Second', 'Third'])
        self.assertEqual(parsed['Thing']['effect']['key'], 'a\nb')
        self.assertEqual(parsed['Thing']['name'], 'ONE TWO')

    def test_gon_rejects_malformed_structures(self):
        for value in ('Thing {\n name A', 'Thing {\n set [A B', '}'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_gon(value)

    def test_loose_zh_and_zh_cn_override_packed_translation(self):
        packed = localization([{'KEY': 'A', 'en': 'English', 'zh-cn': '包内'}])
        for column in ('zh', 'zh-cn'):
            loose = localization([{'KEY': 'A', column: '本地'}], ('KEY', column))
            self.assertEqual(translations(packed, loose)('A'), '本地')
        # A missing local Chinese cell must not replace Chinese with English.
        loose = localization([{'KEY': 'A', 'en': 'Local English'}], ('KEY', 'en', 'zh'))
        self.assertEqual(translations(packed, loose)('A'), '包内')

    def test_english_fallback_and_missing_names(self):
        translate = translations(localization([{'KEY': 'A', 'en': 'Fallback'}]))
        self.assertEqual(translate('A'), 'Fallback')
        with self.assertRaisesRegex(ValueError, 'Missing localization'):
            translate('MISSING')

    def test_multiple_sets_and_wildcard_are_separate(self):
        catalog, wildcard = build_catalog(*fixture())
        by_id = {entry.ident: entry for entry in catalog}
        self.assertEqual([m.ident for m in by_id['Bronze'].members], ['HybridHat'])
        self.assertEqual([m.ident for m in by_id['Cloth'].members], ['HybridHat', 'WildMask'])
        self.assertEqual(wildcard, ['WildMask'])
        self.assertEqual(by_id['Unused'].members, ())

    def test_bonus_includes_stats_omitted_from_description(self):
        catalog, _ = build_catalog(*fixture())
        by_id = {entry.ident: entry for entry in catalog}
        self.assertEqual(by_id['Bronze'].effect, '速度 -1\n护盾 +4\n被攻击时获得 护盾。')
        self.assertEqual(by_id['Cloth'].effect, '敏捷 +2')
        self.assertEqual(by_id['Cloth'].pieces, 2)

    def test_clean_text_retains_semantics_and_rejects_unknown_icons(self):
        self.assertEqual(clean_text('[color:red]体\u200b质[/color] +2\\n[img:divineshield]&nbsp;+1'),
                         '体质 +2\n神圣护盾\u00a0+1')
        with self.assertRaisesRegex(ValueError, 'Unhandled text icon'):
            clean_text('[img:not-known]')

    def test_unknown_set_is_a_build_error(self):
        sets, items, translate = fixture()
        with self.assertRaisesRegex(ValueError, 'Unknown item set'):
            build_catalog(sets, {'test.gon': items['test.gon'].replace('Bronze, Cloth', 'Missing')}, translate)

    def test_generated_header_is_stable_and_empty_arrays_are_portable(self):
        catalog, wildcard = build_catalog(*fixture())
        header = generate_header(catalog, wildcard)
        self.assertEqual(header, generate_header(catalog, wildcard))
        self.assertIn('#include "catalog_types.hpp"', header)
        self.assertIn('"Unused", "未使用套装", nullptr, 0,', header)
        self.assertNotIn('kSetMembers2[]', header)
        self.assertIn('速度 -1\\n护盾 +4', header)
        self.assertIn('kWildcardItemCount = 1', header)

    def test_archive_reads_named_payload_and_rejects_truncation(self):
        name, content = b'data/a.txt', '你好'.encode('utf-8')
        packed = struct.pack('<IH', 1, len(name)) + name + struct.pack('<I', len(content)) + content
        with tempfile.TemporaryDirectory(prefix='item-set-catalog-') as temp:
            path = Path(temp) / 'test.gpak'
            path.write_bytes(packed)
            self.assertEqual(GameArchive(path).text(name.decode()), '你好')
            path.write_bytes(packed[:-1])
            with self.assertRaisesRegex(ValueError, 'does not cover'):
                GameArchive(path)


@unittest.skipUnless((ROOT.parents[1] / 'resources.gpak').is_file(), 'local game archive not available')
class InstalledCatalogTests(unittest.TestCase):
    def test_installed_catalog_is_complete_and_plain_text(self):
        catalog, wildcard = load_catalog(ROOT.parents[1])
        by_id = {item_set.ident: item_set for item_set in catalog}
        self.assertEqual(len(by_id), len(catalog))
        self.assertGreater(len(catalog), 90)
        self.assertGreater(sum(len(s.members) for s in catalog), 800)
        for item_set in catalog:
            with self.subTest(item_set=item_set.ident):
                self.assertGreater(item_set.pieces, 0)
                self.assertTrue(item_set.name)
                self.assertTrue(item_set.effect)
                self.assertNotIn('[img:', item_set.effect)
                self.assertNotIn('\u200b', item_set.effect)
                self.assertEqual(len({m.ident for m in item_set.members}), len(item_set.members))
                self.assertTrue(all(m.name for m in item_set.members))
        # Pure-stat sets have empty source desc; their bonus must remain visible.
        self.assertIn('护盾 +3', by_id['Leather'].effect)
        self.assertIn('速度 +2', by_id['Rag'].effect)
        self.assertIn('RuneofPerthro', wildcard)
        # The compatibility file uses zh, so this also catches locale precedence regressions.
        local = ROOT.parents[1] / 'data/text/combined.csv'
        if local.is_file():
            rows = {r['KEY']: r for r in csv.DictReader(io.StringIO(local.read_text(encoding='utf-8-sig')))}
            expected = rows.get('SETBONUS_ALLOY_NAME', {}).get('zh')
            if expected:
                self.assertEqual(by_id['Alloy'].name, clean_text(expected))


if __name__ == '__main__':
    unittest.main()
