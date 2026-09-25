"""Read named installed-game archive entries without extracting the game.

Adapted from the local RoomCatList project's independently authored reader.
This module is self-contained; ItemSetInfo has no sibling-Mod dependency.
"""
from pathlib import Path
import struct


class GameArchive:
    def __init__(self, path):
        self.path = Path(path)
        self.entries = {}
        with self.path.open('rb') as stream:
            def exact(size):
                value = stream.read(size)
                if len(value) != size:
                    raise ValueError('Truncated game archive index')
                return value

            count = struct.unpack('<I', exact(4))[0]
            if count > 1_000_000:
                raise ValueError('Invalid game archive index')
            offset = 0
            for _ in range(count):
                length = struct.unpack('<H', exact(2))[0]
                name = exact(length).decode('utf-8')
                size = struct.unpack('<I', exact(4))[0]
                if name in self.entries:
                    raise ValueError('Duplicate game archive entry: ' + name)
                self.entries[name] = (offset, size)
                offset += size
            self.base = stream.tell()
        if self.base + offset != self.path.stat().st_size:
            raise ValueError('Game archive index does not cover its payload')

    def text(self, name):
        offset, size = self.entries[name]
        with self.path.open('rb') as stream:
            stream.seek(self.base + offset)
            value = stream.read(size)
        if len(value) != size:
            raise ValueError('Truncated archive entry: ' + name)
        return value.decode('utf-8-sig')
