# Нарезает скриншоты теста для руководства: python3 crop.py <папка со скриншотами> <папка img>
import sys
from PIL import Image

src, out = sys.argv[1].rstrip("/") + "/", sys.argv[2].rstrip("/") + "/"


def save(im, name):
    im.convert("RGB").save(out + name + ".jpg", quality=88)


# координаты — в базовом размере окна 1120×800
TOOLBAR = (14, 14, 1106, 66)
RACK = (14, 76, 1106, 180)
VIEW = (14, 190, 1106, 426)
PANEL = (14, 436, 1106, 786)

macro = Image.open(src + "ui_rubber_macro.png")
save(macro, "main")
save(Image.open(src + "ui_oxygen_macro.png"), "main_oxygen")
save(macro.crop(TOOLBAR), "toolbar")
save(macro.crop(RACK), "rack")
save(Image.open(src + "ui_rubber_parallel.png").crop(RACK), "rack_parallel")
save(Image.open(src + "ui_rubber_rack8.png").crop(RACK), "rack8")
save(macro.crop(VIEW), "view_curves")
save(Image.open(src + "ui_rubber_scope.png").crop(VIEW), "view_scope")
save(macro.crop(PANEL), "panel_macro")
save(Image.open(src + "ui_rubber_advanced.png").crop(PANEL), "panel_advanced")
ott = Image.open(src + "ui_rubber_ott.png")
save(ott.crop(VIEW), "view_stack")
save(ott.crop(PANEL), "panel_ott")
legion = Image.open(src + "ui_rubber_legion.png")
save(legion.crop(VIEW), "view_crowd")
save(legion.crop(PANEL), "panel_legion")
psalter = Image.open(src + "ui_rubber_psalter.png")
save(psalter.crop(VIEW), "view_strings")
save(psalter.crop(PANEL), "panel_psalter")
# окно своих сэмплов: карточка 560×268 по центру панели слота
save(Image.open(src + "ui_rubber_psalter_samples.png").crop((272, 469, 848, 753)), "panel_samples")
