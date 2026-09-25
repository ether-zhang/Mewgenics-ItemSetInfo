"""Generate only the installed game's item-set reference data for this Mod."""
from collections import defaultdict
import csv
from dataclasses import dataclass
import html
import io
import json
from pathlib import Path
import re

from .game_archive import GameArchive
from .gon import parse_gon


STATS = {
    'str': '力量', 'dex': '敏捷', 'con': '体质', 'int': '智力',
    'spd': '速度', 'cha': '魅力', 'lck': '幸运',
    'shield': '护盾', 'divine_shield': '神圣护盾',
}
ICONS = dict(STATS, divineshield='神圣护盾', hp='生命', mp='魔力',
             health='生命', mana='魔力', movement='移动')


@dataclass(frozen=True)
class Member:
    ident: str
    name: str


@dataclass(frozen=True)
class ItemSet:
    ident: str
    name: str
    members: tuple
    pieces: int
    effect: str


def clean_text(text):
    """Retain readable semantics while removing native visual formatting."""
    text = html.unescape(text).replace('\u200b', '').replace('\u00ad', '')
    text = text.replace('\\n', '\n').replace('\r\n', '\n').replace('\r', '\n')

    def icon(match):
        ident = match[1]
        if ident not in ICONS:
            raise ValueError('Unhandled text icon: ' + ident)
        return ICONS[ident]

    text = re.sub(r'\[img:([^\]]+)\]', icon, text)
    text = re.sub(r'\[(?:/?[biu]|/?color(?::[^\]]*)?|s:[^\]]+|/s)\]', '', text)
    return text.strip()


def translation_rows(text):
    result = {}
    for row in csv.DictReader(io.StringIO(text.lstrip('\ufeff'))):
        if row.get('KEY'):
            result[row['KEY']] = row
    return result


def translations(packed, loose=''):
    """Loose Chinese columns override packed Chinese; English is last fallback."""
    packed_rows, loose_rows = translation_rows(packed), translation_rows(loose)

    def translate(key, *, allow_empty=False):
        base = packed_rows.get(key, {})
        overlay = loose_rows.get(key, {})
        if not base and not overlay:
            raise ValueError('Missing localization key: ' + key)
        for row, languages in ((overlay, ('zh-cn', 'zh')), (base, ('zh-cn', 'zh')),
                               (overlay, ('en',)), (base, ('en',))):
            for language in languages:
                if row.get(language):
                    return clean_text(row[language])
        if allow_empty:
            return ''
        raise ValueError('Empty localization: ' + key)

    return translate


def scalar(node, key, default=''):
    value = node.get(key, default)
    if not isinstance(value, str):
        raise ValueError('Expected scalar field: ' + key)
    return value


def set_ids(value):
    if isinstance(value, str):
        return [value]
    if not isinstance(value, list) or any(not isinstance(v, str) for v in value):
        raise ValueError('Invalid item set membership')
    return list(dict.fromkeys(value))


def effect_text(definition, translate):
    lines = []
    for field, label in STATS.items():
        if field in definition:
            amount = float(scalar(definition, field))
            if not -10000 <= amount <= 10000:
                raise ValueError('Invalid set stat amount: ' + field)
            if amount:
                lines.append(f'{label} {amount:+g}')
    description = translate(scalar(definition, 'desc'), allow_empty=True)
    if description:
        lines.append(description)
    if not lines:
        raise ValueError('Set bonus has no readable effects')
    allowed = {'name', 'desc', 'pieces_required', 'passives', 'name_mod'} | STATS.keys()
    unsupported = definition.keys() - allowed
    if unsupported:
        raise ValueError('Unhandled set bonus fields: ' + ', '.join(sorted(unsupported)))
    return '\n'.join(lines)


def build_catalog(set_text, item_texts, translate):
    definitions = parse_gon(set_text)
    members, wildcard, item_ids = defaultdict(list), [], set()
    for source, text in sorted(item_texts.items()):
        for ident, definition in parse_gon(text).items():
            if not isinstance(definition, dict) or 'set' not in definition:
                continue
            if ident in item_ids:
                raise ValueError('Duplicate item definition: ' + ident)
            item_ids.add(ident)
            member = Member(ident, translate(scalar(definition, 'name')))
            for set_id in set_ids(definition['set']):
                if set_id == '*':
                    wildcard.append(ident)
                elif set_id not in definitions:
                    raise ValueError(f'Unknown item set {set_id}: {source}:{ident}')
                else:
                    members[set_id].append(member)
    result = []
    for ident, definition in sorted(definitions.items()):
        if not isinstance(definition, dict):
            raise ValueError('Invalid set definition: ' + ident)
        required = int(scalar(definition, 'pieces_required'))
        if not 1 <= required <= 100:
            raise ValueError('Invalid required set pieces: ' + ident)
        result.append(ItemSet(ident, translate(scalar(definition, 'name')),
                              tuple(sorted(members[ident], key=lambda m: m.ident)),
                              required, effect_text(definition, translate)))
    if not result or not item_ids:
        raise ValueError('Empty item-set catalog')
    return result, sorted(set(wildcard))


def load_catalog(game_root):
    game_root = Path(game_root)
    archive = GameArchive(game_root / 'resources.gpak')

    def text(name):
        path = game_root / name
        return path.read_text(encoding='utf-8-sig') if path.is_file() else archive.text(name)

    loose_text = game_root / 'data/text/combined.csv'
    translate = translations(archive.text('data/text/combined.csv'),
                             loose_text.read_text(encoding='utf-8-sig') if loose_text.is_file() else '')
    item_paths = [name for name in archive.entries
                  if name.startswith('data/items/') and name.endswith('.gon')]
    return build_catalog(text('data/item_setbonuses.gon'),
                         {name: text(name) for name in item_paths}, translate)


def generate_header(catalog, wildcard):
    literal = lambda value: json.dumps(value, ensure_ascii=False)
    lines = [
        '// Generated from this installation. Do not commit or redistribute game text.',
        '#pragma once', '#include "catalog_types.hpp"', '', 'namespace itemsets {',
    ]
    for index, item_set in enumerate(catalog):
        if item_set.members:
            lines.append(f'inline constexpr SetMember kSetMembers{index}[] = {{')
            lines.extend(f'    {{{literal(m.ident)}, {literal(m.name)}}},' for m in item_set.members)
            lines.append('};')
        lines += [f'inline constexpr SetBonus kSetBonuses{index}[] = {{',
                  f'    {{{item_set.pieces}, {literal(item_set.effect)}}},', '};']
    lines.append('inline constexpr ItemSet kItemSets[] = {')
    for index, item_set in enumerate(catalog):
        pointer = f'kSetMembers{index}' if item_set.members else 'nullptr'
        lines.append(f'    {{{literal(item_set.ident)}, {literal(item_set.name)}, {pointer}, '
                     f'{len(item_set.members)}, kSetBonuses{index}, 1}},')
    lines += ['};', 'inline constexpr const char* kWildcardItemIds[] = {']
    lines.extend(f'    {literal(ident)},' for ident in wildcard)
    if not wildcard:
        lines.append('    nullptr,')
    lines += ['};', f'inline constexpr std::size_t kWildcardItemCount = {len(wildcard)};',
              '}  // namespace itemsets', '']
    return '\n'.join(lines)


def generate(game_root, output, report=None):
    catalog, wildcard = load_catalog(game_root)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generate_header(catalog, wildcard), encoding='utf-8')
    summary = {
        'sets': len(catalog),
        'item_memberships': sum(len(s.members) for s in catalog),
        'items': len({m.ident for s in catalog for m in s.members}),
        'max_members_per_set': max(len(s.members) for s in catalog),
        'sets_without_explicit_members': [s.ident for s in catalog if not s.members],
        'wildcard_item_ids': wildcard,
        'source': 'installed resources.gpak with loose game-data overrides',
        'language_precedence': 'loose Chinese, archive Chinese, loose English, archive English',
    }
    if report:
        report = Path(report)
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return summary
