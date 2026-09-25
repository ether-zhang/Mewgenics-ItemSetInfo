"""Build this independent Mod's small catalog from the user's game installation."""
import argparse
import json
from pathlib import Path

from catalog.build_catalog import generate


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, default=root.parents[1])
    parser.add_argument('--output', type=Path, default=root / 'src/set_catalog.generated.hpp')
    parser.add_argument('--report', type=Path, default=root / 'build/catalog-report.json')
    args = parser.parse_args()
    print(json.dumps(generate(args.game_root, args.output, args.report), ensure_ascii=True))


if __name__ == '__main__':
    main()
