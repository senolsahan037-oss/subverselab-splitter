from PIL import Image

def remove_black_background(input_path, output_path):
    img = Image.open(input_path).convert("RGBA")
    datas = img.getdata()

    newData = []
    # Loop through every pixel
    for item in datas:
        # If the pixel is mostly black (R, G, B all < 20)
        # We can also do a luma check or alpha blend it, but let's do a simple blend
        r, g, b, a = item
        # Calculate perceived brightness
        brightness = (r + g + b) / 3.0
        if brightness < 15:
            # Fully transparent for pure black
            newData.append((0, 0, 0, 0))
        elif brightness < 80:
            # Semi transparent for dark glows
            alpha = int((brightness - 15) / 65 * 255)
            newData.append((r, g, b, alpha))
        else:
            newData.append(item)

    img.putdata(newData)
    img.save(output_path, "PNG")

remove_black_background('/Users/senolsahan/.gemini/antigravity-ide/brain/8a8ee99b-1de9-42b2-8a81-caf50e908d47/s_logo_1784412740861.png', '/Users/senolsahan/.gemini/antigravity-ide/scratch/SubverseSplitterPlugin/Resources/logo.png')
