"""Compile the reviewed UTF-8 catalogue to portable UTF-16 C literals."""
import json
from pathlib import Path
import re
import sys


def generate(source, destination):
    translations = json.loads(Path(source).read_text(encoding="utf-8"))
    # Mnemonics/shortcuts remain command metadata; generate actual spellings
    # used by the source instead of translating registry keys or filenames.
    for path in Path("src").glob("*.c"):
        for raw in re.findall(r'"(?:[^"\\]|\\.)*"', path.read_text(encoding="utf-8")):
            try:
                text = json.loads(raw)
            except ValueError:
                continue
            if text in translations or "\0" in text:
                continue
            label, tab, shortcut = text.partition("\t")
            plain = label.replace("&", "")
            ellipsis = "..." if plain.endswith("...") else ""
            base = plain[:-3] if ellipsis else plain
            translated = translations.get(plain, translations.get(base))
            if translated:
                if ellipsis and not translated.endswith("..."):
                    translated += "..."
                if "&" in label and "&" not in translated:
                    translated = "&" + translated
                translations[text] = translated + (tab + shortcut if tab else "")
    lines = ["/* Generated from resources/translations/ru.json; do not edit. */"]
    for english, russian in sorted(translations.items()):
        russian.encode("cp1251")
        if not english or not russian or "\0" in english or "\0" in russian:
            raise ValueError("Empty or embedded-NUL translation")
        lines.append("{%s, L%s, NULL}," % (json.dumps(english), json.dumps(russian)))
    output = Path(destination)
    output.parent.mkdir(parents=True, exist_ok=True)
    content="\n".join(lines) + "\n"
    if not output.exists() or output.read_text(encoding="ascii")!=content:
        output.write_text(content, encoding="ascii")


if __name__ == "__main__":
    generate(*sys.argv[1:])
