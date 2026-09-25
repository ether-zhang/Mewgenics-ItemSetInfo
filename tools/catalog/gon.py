"""Small parser for the game's line-based GON objects and arrays.

The algorithm extends the local RoomCatList parser with real arrays, commas,
and strict structural checks. No game content is bundled in this module.
"""
import json
import re


def parse_gon(text):
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    tokens = re.findall(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\n|[{}\[\],]|[^\s{}\[\],"]+',
        text, re.S)
    tokens = [token for token in tokens if not token.startswith(('//', '/*'))]
    position = 0

    def atom(token):
        return json.loads(token) if token.startswith('"') else token

    def value():
        nonlocal position
        token = tokens[position]
        position += 1
        if token == '{':
            return block(True)
        if token == '[':
            values = []
            while position < len(tokens):
                if tokens[position] == ']':
                    position += 1
                    return values
                if tokens[position] in ('\n', ','):
                    position += 1
                    continue
                values.append(value())
            raise ValueError('Unclosed GON array')
        if token in ('}', ']', ','):
            raise ValueError('Unexpected GON delimiter: ' + token)
        return atom(token)

    def block(nested=False):
        nonlocal position
        result = {}
        while position < len(tokens):
            token = tokens[position]
            position += 1
            if token == '\n':
                continue
            if token == '}':
                if not nested:
                    raise ValueError('Unexpected GON close brace')
                return result
            if token in ('{', '[', ']', ','):
                raise ValueError('Expected GON key')
            key, values = atom(token), []
            while position < len(tokens) and tokens[position] not in ('\n', '}'):
                current = value()
                values.append(current)
                if isinstance(current, dict):
                    break
            result[key] = values[0] if len(values) == 1 else values
        if nested:
            raise ValueError('Unclosed GON block')
        return result

    return block()
