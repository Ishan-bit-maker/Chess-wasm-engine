import struct, math, random, os

with open('/tmp/ref.bmp', 'rb') as f:
    header = f.read(54)
    width, height = struct.unpack('<ii', header[18:26])
    top_down = height < 0
    height = abs(height)
    row_size = ((width * 3 + 3) // 4) * 4
    f.seek(54)
    data = f.read()

def get_pixel(x, y):
    bmp_y = y if top_down else (height - 1 - y)
    offset = bmp_y * row_size + x * 3
    b, g, r = data[offset], data[offset+1], data[offset+2]
    return (r, g, b)

def save_bmp(w, h, get_pix_fn, out_path):
    out_row_size = ((w * 3 + 3) // 4) * 4
    image_size = out_row_size * h
    file_size = 54 + image_size
    
    # 24-bit uncompressed BMP header
    header = bytearray(54)
    header[0:2] = b'BM'
    header[2:6] = struct.pack('<I', file_size)
    header[10:14] = struct.pack('<I', 54)
    header[14:18] = struct.pack('<I', 40) # DIB header size
    header[18:22] = struct.pack('<i', w)
    header[22:26] = struct.pack('<i', -h) # top-down
    header[26:28] = struct.pack('<H', 1)  # planes
    header[28:30] = struct.pack('<H', 24) # bpp
    header[34:38] = struct.pack('<I', image_size)
    header[38:42] = struct.pack('<i', 2835) # 72 DPI
    header[42:46] = struct.pack('<i', 2835)
    
    buf = bytearray(image_size)
    for y in range(h):
        row_offset = y * out_row_size
        for x in range(w):
            r, g, b = get_pix_fn(x, y)
            r = min(255, max(0, int(r)))
            g = min(255, max(0, int(g)))
            b = min(255, max(0, int(b)))
            px_offset = row_offset + x * 3
            buf[px_offset] = b
            buf[px_offset+1] = g
            buf[px_offset+2] = r
            
    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(buf)

# 1. Crop Light Wood Square (from e4: x=480..544, y=400..464)
# Let's crop a 64x64 patch and mirror edges for seamless tiling
print("Generating light wood texture...")
def get_light_wood(x, y):
    # crop region in ref: x in 480..543, y in 400..463
    sx = 480 + (x % 64)
    sy = 400 + (y % 64)
    return get_pixel(sx, sy)

save_bmp(128, 128, lambda x, y: get_light_wood(x % 64, y % 64), '/tmp/wood_light.bmp')

# 2. Crop Dark Wood Square (from d4: x=420..470, y=400..450, or from e5: x=480..540, y=340..390)
print("Generating dark wood texture...")
def get_dark_wood(x, y):
    # in ref image, d5 (dark): x=415..475, y=335..395
    sx = 420 + (x % 56)
    sy = 340 + (y % 56)
    return get_pixel(sx, sy)

save_bmp(128, 128, lambda x, y: get_dark_wood(x % 56, y % 56), '/tmp/wood_dark.bmp')

# 3. Crop Frame Wood Texture (from bottom frame: x=360..600, y=665..705)
print("Generating frame wood texture...")
def get_frame_wood(x, y):
    sx = 360 + (x % 240)
    sy = 665 + (y % 38)
    return get_pixel(sx, sy)

save_bmp(256, 128, lambda x, y: get_frame_wood(x % 240, y % 38), '/tmp/wood_frame.bmp')

# 4. Generate Studio Background (1920x1080)
# Matches the reference image's dark slate studio table lighting profile:
# - Center of highlight: cx=960, cy=180
# - Color falloff from dark warm grey (~#424040 at spot) to deep charcoal (~#0a0a0a at edges)
# - Fine organic table noise
print("Generating 1920x1080 studio background...")
random.seed(42)

# Pre-generate noise table
noise_w, noise_h = 256, 256
noise_grid = [[random.gauss(0, 2.8) for _ in range(noise_w)] for _ in range(noise_h)]

def get_bg_pixel(x, y):
    # Normalized coords relative to center
    dx = (x - 960) / 960.0
    dy = (y - 200) / 700.0
    
    # Elliptical distance from key light
    dist_sq = dx*dx * 1.1 + dy*dy * 0.95
    dist = math.sqrt(dist_sq)
    
    # Lighting intensity curve
    # At center (dist=0): brightness ~ 72
    # At dist=1: brightness ~ 45
    # At dist=1.5: brightness ~ 16
    # At corners (dist > 1.8): brightness ~ 8
    spot = math.exp(-dist_sq * 0.9)
    vignette = math.exp(-dist_sq * 0.45)
    
    base_val = 10.0 + 34.0 * vignette + 28.0 * spot
    
    # Add subtle organic noise
    n = noise_grid[y % noise_h][x % noise_w]
    val = base_val + n
    
    # Subtle warm tint (R slightly higher than G and B, like slate under tungsten studio light)
    r = val * 1.02
    g = val * 0.99
    b = val * 0.98
    
    return (r, g, b)

save_bmp(1920, 1080, get_bg_pixel, '/tmp/studio_bg.bmp')
print("All BMP assets generated successfully!")
