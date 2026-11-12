import sys, os, struct
from PIL import Image, ImageDraw
W, H, R = 410, 502, 65
src, dst = sys.argv[1], sys.argv[2]
for n in sorted(os.listdir(src)):
    if not n.endswith('.raw'): continue
    d = open(os.path.join(src, n), 'rb').read()
    px = struct.unpack('<%dH' % (W * H), d)
    im = Image.new('RGB', (W, H))
    im.putdata([(((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31) for p in px])
    # mask to the AMOLED's rounded shape so the preview looks like the watch
    m = Image.new('L', (W, H), 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, W - 1, H - 1], R, fill=255)
    out = Image.new('RGB', (W, H), (24, 24, 28)); out.paste(im, (0, 0), m)
    out.save(os.path.join(dst, n.replace('.raw', '.png')))
print('pngs ->', dst)
