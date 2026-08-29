"""Convert a raw 240x160 RGB565 frame dump (smoke_frame.rgb565) to PNG.

Usage: python tools/rgb565_to_png.py <input.rgb565> <output.png>
"""
import sys
from PIL import Image

W, H = 240, 160

def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = f.read()
    assert len(data) == W * H * 2, f"expected {W*H*2} bytes, got {len(data)}"
    # PIL raw mode "BGR;16" = little-endian RGB565.
    img = Image.frombytes("RGB", (W, H), data, "raw", "BGR;16")
    img = img.resize((W * 3, H * 3), Image.NEAREST)
    img.save(dst)
    print(f"wrote {dst} ({W*3}x{H*3})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
