"""Check locale JSON and common text corruption without building the app."""

import json
from pathlib import Path
import re
import sys


REQUIRED_KEYS = {
    "pages.preference.model.behaviorModal.labels.clearMotions",
    "pages.preference.model.behaviorModal.labels.clearExpression",
    "pages.preference.model.behaviorModal.labels.stopAllAudio",
    "pages.preference.model.behaviorModal.labels.selectAll",
    "pages.preference.model.behaviorModal.hints.randomPool",
    "native.startup.openglUnavailable",
}
CORRUPTION = re.compile(r"\?{2,}|\w\?\w|\ufffd")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def flatten(value, prefix=""):
    if isinstance(value, dict):
        for key, item in value.items():
            yield from flatten(item, f"{prefix}.{key}" if prefix else key)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from flatten(item, f"{prefix}[{index}]")
    else:
        yield prefix, value


def main():
    root = Path(__file__).resolve().parents[1] / "resources/assets/locales"
    files = sorted(root.glob("*.json"))
    errors = []
    if not files:
        errors.append(f"No locale files found in {root}")
    for path in files:
        try:
            document = json.loads(path.read_text(encoding="utf-8-sig"),
                                  object_pairs_hook=unique_object)
            if not isinstance(document, dict):
                raise ValueError("locale root must be an object")
            values = dict(flatten(document))
            for key in sorted(REQUIRED_KEYS):
                if not isinstance(values.get(key), str) or not values[key].strip():
                    errors.append(f"{path.name}: missing or empty label: {key}")
            for key, value in values.items():
                if isinstance(value, str) and CORRUPTION.search(value):
                    errors.append(f"{path.name}: possible text corruption: {key}")
        except (OSError, UnicodeError, ValueError) as error:
            errors.append(f"{path.name}: {error}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Validated {len(files)} locales: JSON, duplicate keys, required labels, text corruption.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
