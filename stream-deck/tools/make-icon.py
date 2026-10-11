"""Draw the plugin's marketplace icon (PNG, which the manifest requires): a red busy light."""
from pathlib import Path
from PIL import Image, ImageDraw

out = Path(__file__).resolve().parent.parent / 'io.github.cyamato.akashicbusy.sdPlugin' / 'imgs' / 'plugin'
for name, size in (('marketplace.png', 288), ('marketplace@2x.png', 512)):
    s = size / 288
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=int(48 * s), fill='#151a33')
    d.rounded_rectangle([int(54 * s), int(70 * s), int(234 * s), int(190 * s)], radius=int(14 * s), outline='#e9ecf7', width=int(12 * s))
    d.line([int(144 * s), int(190 * s), int(144 * s), int(226 * s)], fill='#e9ecf7', width=int(12 * s))
    d.line([int(104 * s), int(226 * s), int(184 * s), int(226 * s)], fill='#e9ecf7', width=int(12 * s))
    d.ellipse([int(114 * s), int(100 * s), int(174 * s), int(160 * s)], fill='#dc2626')
    img.save(out / name)
    print('wrote', out / name)
