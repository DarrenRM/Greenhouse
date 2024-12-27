from PIL import Image
import os

def create_ico(png_path, ico_path):
    # Open the PNG image
    img = Image.open(png_path)

    # Convert to RGBA if not already
    img = img.convert('RGBA')

    # Standard Windows icon sizes
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    
    # Create temporary images for each size
    imgs = []
    for size in sizes:
        resized_img = img.resize(size, Image.Resampling.LANCZOS)
        imgs.append(resized_img)

    # If icon exists, remove it first
    if os.path.exists(ico_path):
        os.remove(ico_path)

    # Save as ICO with all sizes
    imgs[0].save(
        ico_path,
        format='ICO',
        append_images=imgs[1:],
        sizes=sizes,
        optimize=True
    )

# Create the icon
create_ico('app/greenhouse_icon.png', 'app/icon.ico') 