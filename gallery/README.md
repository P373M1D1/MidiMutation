# Gallery Image Folder

Drop your source images here. Any common format works (JPEG, PNG, BMP, etc.).

The conversion script will rescale them to **448×300 pixels** (RGB565) and compile
them directly into the firmware as C arrays.

## How to add images

1. Copy your image files into this folder.
2. Run the generator script from the project root:

   ```
   python tools/gen_gallery.py
   ```

3. Rebuild the firmware (`cmake --build build/Debug --target ChatTest`).
4. The new images appear in **GLOBAL → Gallery** and can be scrolled with **ENC2**.

## Notes

- Images are compiled into flash; each 448×300 image occupies **268 800 bytes**.
- Keep the total number of gallery images small to avoid overflowing flash.
- The script reads files alphabetically; rename them with a numeric prefix
  (e.g. `01_logo.png`, `02_band.jpg`) to control display order.
- Supported input: anything Pillow can open (JPEG, PNG, BMP, GIF first frame, WEBP, …).
- Install Pillow once with: `pip install Pillow`
