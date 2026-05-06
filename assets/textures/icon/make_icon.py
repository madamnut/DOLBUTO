from __future__ import annotations

import struct
from io import BytesIO
from pathlib import Path

from PIL import Image


SOURCE_FILE = "icon.png"
OUTPUT_FILE = "icon.ico"
BASIS_SIZE = 32
ICON_SIZES = (16, 32, 48, 64, 128, 256)


def _png_bytes(image: Image.Image) -> bytes:
    output = BytesIO()
    image.save(output, format="PNG")
    return output.getvalue()


def main() -> None:
    icon_dir = Path(__file__).resolve().parent
    source_path = icon_dir / SOURCE_FILE
    output_path = icon_dir / OUTPUT_FILE

    if not source_path.exists():
        raise FileNotFoundError(f"missing source icon: {source_path}")

    source = Image.open(source_path).convert("RGBA")
    basis = source.resize((BASIS_SIZE, BASIS_SIZE), Image.Resampling.NEAREST)

    images = []
    for size in ICON_SIZES:
        resized = basis.resize((size, size), Image.Resampling.NEAREST)
        images.append((size, _png_bytes(resized)))

    header_size = 6
    entry_size = 16
    image_offset = header_size + entry_size * len(images)

    with output_path.open("wb") as output:
        output.write(struct.pack("<HHH", 0, 1, len(images)))

        offset = image_offset
        for size, data in images:
            output.write(
                struct.pack(
                    "<BBBBHHII",
                    0 if size == 256 else size,
                    0 if size == 256 else size,
                    0,
                    0,
                    1,
                    32,
                    len(data),
                    offset,
                )
            )
            offset += len(data)

        for _, data in images:
            output.write(data)

    print(f"created {output_path}")


if __name__ == "__main__":
    main()
