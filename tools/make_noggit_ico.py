from PIL import Image
from pathlib import Path

src = Path("media/noggit_icon.png")
img = Image.open(src).convert("RGBA")
sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
icons = [img.resize(s, Image.Resampling.LANCZOS) for s in sizes]
ico_path = Path("media/noggit.ico")
icons[0].save(
    ico_path,
    format="ICO",
    sizes=[(i.width, i.height) for i in icons],
    append_images=icons[1:],
)
print(f"wrote {ico_path} ({ico_path.stat().st_size} bytes) from {src} {img.size}")
