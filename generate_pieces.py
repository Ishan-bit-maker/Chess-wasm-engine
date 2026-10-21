import os

os.makedirs('assets/pieces', exist_ok=True)

# SVG filters and gradients for realistic 3D carved wood appearance
DEFS_WHITE = """
  <defs>
    <!-- White / Boxwood gradients -->
    <radialGradient id="w-wood-rad" cx="35%" cy="28%" r="75%">
      <stop offset="0%" stop-color="#fff9f0"/>
      <stop offset="25%" stop-color="#eedfc9"/>
      <stop offset="60%" stop-color="#d4bfa5"/>
      <stop offset="85%" stop-color="#b69e80"/>
      <stop offset="100%" stop-color="#8a7055"/>
    </radialGradient>
    <linearGradient id="w-wood-vert" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="#ffffff" stop-opacity="0.9"/>
      <stop offset="15%" stop-color="#fdf5ea" stop-opacity="0.5"/>
      <stop offset="70%" stop-color="#c4ac8f" stop-opacity="0.2"/>
      <stop offset="100%" stop-color="#6e553c" stop-opacity="0.7"/>
    </linearGradient>
    <linearGradient id="w-sheen" x1="20%" y1="0%" x2="80%" y2="100%">
      <stop offset="0%" stop-color="#ffffff" stop-opacity="0.8"/>
      <stop offset="40%" stop-color="#f5e8d5" stop-opacity="0.2"/>
      <stop offset="100%" stop-color="#4a3622" stop-opacity="0.4"/>
    </linearGradient>
    <!-- Drop shadow for 3D grounding -->
    <filter id="p-shadow" x="-30%" y="-15%" width="160%" height="150%">
      <feDropShadow dx="0" dy="3.5" stdDeviation="2.2" flood-color="#0a0705" flood-opacity="0.6"/>
      <feDropShadow dx="0" dy="1.2" stdDeviation="1" flood-color="#1c120c" flood-opacity="0.4"/>
    </filter>
  </defs>
"""

DEFS_BLACK = """
  <defs>
    <!-- Black / Dark Walnut / Ebony gradients -->
    <radialGradient id="b-wood-rad" cx="35%" cy="25%" r="80%">
      <stop offset="0%" stop-color="#735d4d"/>
      <stop offset="25%" stop-color="#4f3e33"/>
      <stop offset="65%" stop-color="#31251e"/>
      <stop offset="90%" stop-color="#1d1511"/>
      <stop offset="100%" stop-color="#0d0907"/>
    </radialGradient>
    <linearGradient id="b-wood-vert" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="#917763" stop-opacity="0.85"/>
      <stop offset="20%" stop-color="#634f40" stop-opacity="0.4"/>
      <stop offset="70%" stop-color="#241a14" stop-opacity="0.3"/>
      <stop offset="100%" stop-color="#050302" stop-opacity="0.85"/>
    </linearGradient>
    <linearGradient id="b-sheen" x1="20%" y1="0%" x2="80%" y2="100%">
      <stop offset="0%" stop-color="#ab907a" stop-opacity="0.9"/>
      <stop offset="45%" stop-color="#4a392e" stop-opacity="0.3"/>
      <stop offset="100%" stop-color="#000000" stop-opacity="0.8"/>
    </linearGradient>
    <!-- Drop shadow for 3D grounding -->
    <filter id="p-shadow" x="-30%" y="-15%" width="160%" height="150%">
      <feDropShadow dx="0" dy="3.5" stdDeviation="2.2" flood-color="#0a0705" flood-opacity="0.6"/>
      <feDropShadow dx="0" dy="1.2" stdDeviation="1" flood-color="#1c120c" flood-opacity="0.4"/>
    </filter>
  </defs>
"""

def create_svg(piece_key, content, is_white):
    defs = DEFS_WHITE if is_white else DEFS_BLACK
    stroke_col = "#6e553c" if is_white else "#2b2019"
    hi_stroke = "rgba(255,255,255,0.7)" if is_white else "rgba(200,175,150,0.5)"
    
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 45 45" width="100%" height="100%">
{defs}
  <g filter="url(#p-shadow)">
    {content}
  </g>
</svg>"""

# 1. PAWN
pawn_path = "M 22.5 9 c -2.21 0 -4 1.79 -4 4 c 0 0.89 0.29 1.71 0.78 2.38 C 17.33 16.5 16 18.59 16 21 c 0 2.03 0.94 3.84 2.41 5.03 C 15.41 27.09 11 31.58 11 39.5 h 23 c 0 -7.92 -4.41 -12.41 -7.41 -13.47 c 1.47 -1.19 2.41 -3 2.41 -5.03 c 0 -2.41 -1.33 -4.5 -2.78 -5.62 c 0.49 -0.67 0.78 -1.49 0.78 -2.38 c 0 -2.21 -1.79 -4 -4 -4 z"

def pawn_content(is_white):
    fill = "url(#w-wood-rad)" if is_white else "url(#b-wood-rad)"
    stroke = "#543e2a" if is_white else "#18100b"
    inner_stroke = "#fff6ea" if is_white else "#806856"
    return f"""
    <path d="{pawn_path}" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="{pawn_path}" fill="url(#{"w" if is_white else "b"}-wood-vert)" opacity="0.6"/>
    <!-- Collar ring accent -->
    <ellipse cx="22.5" cy="26" rx="4.5" ry="1.2" fill="none" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.7"/>
    <!-- Head specular gloss -->
    <ellipse cx="21" cy="11.5" rx="2.2" ry="1.4" fill="#ffffff" opacity="{0.55 if is_white else 0.28}"/>
    <!-- Base rim -->
    <path d="M 12 39.5 h 21" stroke="{inner_stroke}" stroke-width="0.9" opacity="0.6"/>
    """

# 2. ROOK
def rook_content(is_white):
    fill = "url(#w-wood-rad)" if is_white else "url(#b-wood-rad)"
    stroke = "#543e2a" if is_white else "#18100b"
    inner_stroke = "#fff6ea" if is_white else "#806856"
    return f"""
    <!-- Base rim -->
    <path d="M 9 39.5 h 27 v -3 H 9 v 3 z" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <!-- Pedestal step -->
    <path d="M 12 36.5 v -4 h 21 v 4 H 12 z" fill="{fill}" stroke="{stroke}" stroke-width="1.2" stroke-linejoin="round"/>
    <!-- Tower shaft -->
    <path d="M 13.5 32.5 l 1.5 -16 h 15 l 1.5 16 H 13.5 z" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <!-- Battlements -->
    <path d="M 11 16.5 v -7.5 h 4 v 3 h 5 v -3 h 5 v 3 h 5 v -3 h 4 v 7.5 H 11 z" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <!-- Wood grain vertical overlay -->
    <rect x="9" y="9" width="27" height="30.5" fill="url(#{"w" if is_white else "b"}-wood-vert)" opacity="0.4" clip-path="url(#none)"/>
    <!-- Highlights -->
    <path d="M 15 16.5 h 15" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.7"/>
    <path d="M 13.5 32.5 h 18" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.7"/>
    <line x1="16" y1="18" x2="15.2" y2="31" stroke="{inner_stroke}" stroke-width="1" opacity="{0.6 if is_white else 0.3}"/>
    """

# 3. KNIGHT
knight_base = "M 15 39.5 h 23 c 0 -8.5 -5 -16 -16 -21 c -2 14.5 -12 12 -7 21 z"
knight_head = "M 22 10 C 32.5 11 38.5 18 38 39.5 H 15 C 15 30.5 25 33 23 18 C 24.38 20.91 18.45 25.37 16 27 C 13 29 13.18 31.34 11 31 C 9.95 30.84 4.84 21 10 13 C 13 8.5 18 3 22 5 C 23.92 5.96 24 8.5 24 10 z"

def knight_content(is_white):
    fill = "url(#w-wood-rad)" if is_white else "url(#b-wood-rad)"
    stroke = "#543e2a" if is_white else "#18100b"
    inner_stroke = "#fff6ea" if is_white else "#806856"
    return f"""
    <path d="{knight_head}" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="{knight_head}" fill="url(#{"w" if is_white else "b"}-wood-vert)" opacity="0.45"/>
    <!-- Mane and head curves -->
    <path d="M 22 5 C 26 8 27 13 25 18" fill="none" stroke="{inner_stroke}" stroke-width="0.9" opacity="0.7"/>
    <path d="M 28 13 C 33 17 34 25 33 33" fill="none" stroke="{inner_stroke}" stroke-width="0.9" opacity="0.6"/>
    <!-- Eye -->
    <circle cx="15.5" cy="15.5" r="1.3" fill="none" stroke="{inner_stroke}" stroke-width="0.8"/>
    <circle cx="15.5" cy="15.5" r="0.6" fill="{stroke}"/>
    <!-- Nostril -->
    <path d="M 9.5 24.5 a 0.7 0.7 0 1 1 -0.1 0" fill="{stroke}"/>
    <!-- Snout and cheek bevel -->
    <path d="M 12 21 C 14 20 17 21 18 23" fill="none" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.6"/>
    <path d="M 15 39.5 h 23" stroke="{inner_stroke}" stroke-width="0.9" opacity="0.6"/>
    """

# 4. BISHOP
bishop_mitre = "M 9 36 C 12 39 22 39 24 36 C 27 39 33 39 36 36 C 36 32 30 27 30 21 C 30 16 33 12 33 9 C 30 6 27 6 24 9 C 21 6 18 6 15 9 C 15 12 18 16 18 21 C 18 27 12 32 12 36 Z"

def bishop_content(is_white):
    fill = "url(#w-wood-rad)" if is_white else "url(#b-wood-rad)"
    stroke = "#543e2a" if is_white else "#18100b"
    inner_stroke = "#fff6ea" if is_white else "#806856"
    return f"""
    <!-- Base pedestal -->
    <path d="M 10 39.5 h 25 v -3 H 10 v 3 z" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <!-- Mitre Body -->
    <path d="{bishop_mitre}" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="{bishop_mitre}" fill="url(#{"w" if is_white else "b"}-wood-vert)" opacity="0.5"/>
    <!-- Finial ball on top -->
    <circle cx="24" cy="5.5" r="2.2" fill="{fill}" stroke="{stroke}" stroke-width="1"/>
    <circle cx="23.2" cy="4.8" r="0.7" fill="#ffffff" opacity="{0.6 if is_white else 0.3}"/>
    <!-- Mitre slit / cut -->
    <path d="M 21.5 12 L 27.5 17" stroke="{stroke}" stroke-width="1.5" stroke-linecap="round"/>
    <path d="M 21.5 12 L 27.5 17" stroke="{inner_stroke}" stroke-width="0.6" stroke-linecap="round"/>
    <!-- Collars -->
    <path d="M 14.5 32 h 16" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.7"/>
    <path d="M 12 36 h 21" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.7"/>
    <!-- Specular highlight curve -->
    <path d="M 18 16 C 18 21 21 27 22 30" fill="none" stroke="{inner_stroke}" stroke-width="1" opacity="{0.6 if is_white else 0.3}"/>
    """

# 5. QUEEN
def queen_content(is_white):
    fill = "url(#w-wood-rad)" if is_white else "url(#b-wood-rad)"
    stroke = "#543e2a" if is_white else "#18100b"
    inner_stroke = "#fff6ea" if is_white else "#806856"
    crown_pts = "M 9 26 C 17.5 24.5 30 24.5 36 26 L 39 12.5 L 31.5 24.5 L 30.7 10 L 25.5 24 L 22.5 9 L 19.5 24 L 14.3 10 L 13.5 24.5 L 6 12.5 L 9 26 z"
    skirt_pts = "M 9 26 C 9 28 10.5 28 11.5 30 C 12.5 31.5 12.5 31 12 33.5 C 10.5 34.5 10.5 36 10.5 36 C 9 37.5 11 39.5 11 39.5 C 17.5 40.5 27.5 40.5 34 39.5 C 34 39.5 36 37.5 34.5 36 C 34.5 36 34.5 34.5 33 33.5 C 32.5 31 32.5 31.5 33.5 30 C 34.5 28 36 28 36 26 z"
    return f"""
    <!-- Coronet spheres -->
    <circle cx="6" cy="12" r="2.2" fill="{fill}" stroke="{stroke}" stroke-width="0.9"/>
    <circle cx="14" cy="9" r="2.2" fill="{fill}" stroke="{stroke}" stroke-width="0.9"/>
    <circle cx="22.5" cy="8" r="2.5" fill="{fill}" stroke="{stroke}" stroke-width="0.9"/>
    <circle cx="31" cy="9" r="2.2" fill="{fill}" stroke="{stroke}" stroke-width="0.9"/>
    <circle cx="39" cy="12" r="2.2" fill="{fill}" stroke="{stroke}" stroke-width="0.9"/>
    
    <circle cx="22" cy="7.2" r="0.8" fill="#ffffff" opacity="{0.6 if is_white else 0.3}"/>
    
    <!-- Crown -->
    <path d="{crown_pts}" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <path d="{skirt_pts}" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <path d="{skirt_pts}" fill="url(#{"w" if is_white else "b"}-wood-vert)" opacity="0.5"/>
    <!-- Waistband rings -->
    <path d="M 11 29 C 18 27.5 27 27.5 34 29" fill="none" stroke="{inner_stroke}" stroke-width="0.9" opacity="0.7"/>
    <path d="M 12 33.5 C 18 32.5 27 32.5 33 33.5" fill="none" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.6"/>
    <path d="M 11 37 C 18 36 27 36 34 37" fill="none" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.6"/>
    """

# 6. KING
def king_content(is_white):
    fill = "url(#w-wood-rad)" if is_white else "url(#b-wood-rad)"
    stroke = "#543e2a" if is_white else "#18100b"
    inner_stroke = "#fff6ea" if is_white else "#806856"
    cap_path = "M 22.5 25 C 27 17.5 25.5 14.5 25.5 14.5 C 25.5 14.5 24.5 12 22.5 12 C 20.5 12 19.5 14.5 19.5 14.5 C 19.5 14.5 18 17.5 22.5 25 Z"
    robe_path = "M 11.5 39 C 17 41.5 28 41.5 33.5 39 L 33.5 31 C 33.5 31 42.5 26.5 39.5 20.5 C 35.5 14 26 17 22.5 24.5 L 22.5 27 L 22.5 24.5 C 19 17 9.5 14 5.5 20.5 C 2.5 26.5 11.5 31 11.5 31 L 11.5 39 Z"
    return f"""
    <!-- Cross finial -->
    <path d="M 22.5 4.5 L 22.5 11.5 M 19.5 7 L 25.5 7" stroke="{stroke}" stroke-width="1.8" stroke-linecap="round"/>
    <path d="M 22.5 4.5 L 22.5 11.5 M 19.5 7 L 25.5 7" stroke="{inner_stroke}" stroke-width="0.8" stroke-linecap="round"/>
    <!-- Dome cap -->
    <path d="{cap_path}" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <!-- Robe / Shoulders -->
    <path d="{robe_path}" fill="{fill}" stroke="{stroke}" stroke-width="1.3" stroke-linejoin="round"/>
    <path d="{robe_path}" fill="url(#{"w" if is_white else "b"}-wood-vert)" opacity="0.5"/>
    <!-- Collars -->
    <path d="M 12 32 C 17 29.5 28 29.5 33 32" fill="none" stroke="{inner_stroke}" stroke-width="0.9" opacity="0.7"/>
    <path d="M 11.5 35.5 C 17 33.5 28 33.5 33.5 35.5" fill="none" stroke="{inner_stroke}" stroke-width="0.8" opacity="0.6"/>
    <!-- Specular highlight on left shoulder -->
    <path d="M 12 22 C 14 21 19 25 21 28" fill="none" stroke="{inner_stroke}" stroke-width="1" opacity="{0.6 if is_white else 0.3}"/>
    """

pieces = [
    ('wP', pawn_content(True), True),
    ('wR', rook_content(True), True),
    ('wN', knight_content(True), True),
    ('wB', bishop_content(True), True),
    ('wQ', queen_content(True), True),
    ('wK', king_content(True), True),
    ('bP', pawn_content(False), False),
    ('bR', rook_content(False), False),
    ('bN', knight_content(False), False),
    ('bB', bishop_content(False), False),
    ('bQ', queen_content(False), False),
    ('bK', king_content(False), False),
]

for name, content, is_w in pieces:
    svg_data = create_svg(name, content, is_w)
    with open(f'assets/pieces/{name}.svg', 'w') as f:
        f.write(svg_data)

print(f"Generated {len(pieces)} piece SVGs successfully!")
