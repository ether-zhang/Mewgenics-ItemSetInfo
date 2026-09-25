"""Verify the supported executable and native entry points without running it."""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parents[1]


def read_pe(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError('Expected Windows executable')
    nt = struct.unpack_from('<I', data, 0x3c)[0]
    if data[nt:nt+4] != b'PE\0\0' or struct.unpack_from('<H', data, nt+4)[0] != 0x8664:
        raise ValueError('Expected x64 PE executable')
    count, timestamp = struct.unpack_from('<HI', data, nt+6)
    optional_size = struct.unpack_from('<H', data, nt+20)[0]
    image_size = struct.unpack_from('<I', data, nt+24+56)[0]
    sections = []
    for i in range(count):
        offset = nt+24+optional_size+i*40
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, offset+8)
        sections.append((rva, raw_size, raw))

    def at(rva, size):
        for start, length, raw in sections:
            if start <= rva and rva+size <= start+length:
                return data[raw+rva-start:raw+rva-start+size]
        raise ValueError(f'RVA {rva:#x} is outside file-backed sections')
    return data, timestamp, image_size, at


def main():
    config = json.loads((ROOT/'tools/native_signatures.json').read_text(encoding='utf-8'))
    data, timestamp, image_size, at = read_pe(ROOT.parents[1]/'Mewgenics.exe')
    if hashlib.sha256(data).hexdigest().upper() != config['image_sha256'].upper():
        raise ValueError('This executable differs from the statically verified game build')
    guards = []
    lines = ['// Generated after checking the supported executable and native signatures.',
             '#pragma once', '#include <cstdint>', 'namespace itemsets {',
             f'inline constexpr std::uint32_t kImageTimestamp = 0x{timestamp:X};',
             f'inline constexpr std::uint32_t kImageSize = 0x{image_size:X};',
             'struct NativeSignature { std::uintptr_t rva; const char* name; unsigned char bytes[32]; unsigned size; };']
    for name, definition in config['symbols'].items():
        rva = int(definition['rva'], 0)
        expected = bytes.fromhex(definition['bytes'])
        if not 1 <= len(expected) <= 32 or at(rva, len(expected)) != expected:
            raise ValueError(f'Native signature mismatch: {name}')
        lines.append(f'inline constexpr std::uintptr_t kRva{name} = 0x{rva:X};')
        if 'stolen_bytes' in definition:
            lines.append(f'inline constexpr int kStolen{name} = {int(definition["stolen_bytes"])};')
        guards.append('    {0x%X, "%s", {%s}, %d},' %
                      (rva, name, ', '.join(f'0x{x:02X}' for x in expected), len(expected)))
    lines += ['inline constexpr NativeSignature kNativeSignatures[] = {', *guards, '};', '}', '']
    (ROOT/'src/native_signatures.generated.hpp').write_text('\n'.join(lines), encoding='utf-8')
    print(f'ItemSetInfo native API verified: {len(guards)} code guards. Game was not launched.')


if __name__ == '__main__':
    main()
