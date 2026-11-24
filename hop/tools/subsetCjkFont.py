"""Создать шрифт китайского интерфейса из NotoSansSC[wght].ttf (fonttools)."""
import argparse
import xml.etree.ElementTree as ET
from pathlib import Path
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("destination", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
text = "简体中文"
for catalog in ["hop_zh.ts", "amneziavpn_zh_CN.ts"]:
    for translation in ET.parse(root / "shared/client/translations" / catalog).iter("translation"):
        text += "".join(translation.itertext())
font = TTFont(args.source)
options = subset.Options()
options.name_IDs = ["*"]
options.name_languages = ["*"]
subsetter = subset.Subsetter(options=options)
subsetter.populate(text=text)
subsetter.subset(font)
if "fvar" in font:
    instantiateVariableFont(font, {"wght": 400}, inplace=True)
names = font["name"]
for record in list(names.names):
    if record.nameID in (1, 2, 3, 4, 6, 16, 17):
        replacement = {1: "Hop CJK UI", 2: "Regular", 3: "Hop CJK UI Regular 1.0",
                       4: "Hop CJK UI Regular", 6: "HopCJKUI-Regular", 16: "Hop CJK UI", 17: "Regular"}[record.nameID]
        record.string = replacement.encode(record.getEncoding())
license_text = (root / "shared/client/fonts/hop-cjk-ui-OFL.txt").read_text(encoding="utf-8")
names.setName(license_text, 13, 3, 1, 0x409)
args.destination.parent.mkdir(parents=True, exist_ok=True)
font.save(args.destination)
print(f"{len(font.getBestCmap())} glyphs; {args.destination.stat().st_size} bytes")
