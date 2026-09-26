"""Recreate the checked-in F3 font from the original variable font; not a build step.

Requires fonttools==4.59.0, optionally installed in .cache/font-tools.
"""

from pathlib import Path
import sys

task_root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(task_root / ".cache" / "font-tools"))

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

if __name__ == "__main__":
    with TTFont(task_root / "assets/fonts/NotoSansKR.ttf") as source:
        font = instantiateVariableFont(source, {"wght": 600}, inplace=True, updateFontNames=True)
        font.save(task_root / "assets/fonts/NotoSansKR-SemiBold.ttf")
