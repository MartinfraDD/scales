"""Generate the Scales page artwork (480 x 220).

Two octaves of keys with a dot on each key the scale uses, the scale's name,
and the SDK's keyboard of sounding notes along the bottom.
"""
from pathlib import Path

WHITE = (0, 2, 4, 6, 7, 9, 11)  # key positions from F
BLACK = (1, 3, 5, 8, 10)
BLACK_AFTER = (1, 2, 3, 5, 6)   # the white key each black key straddles
MIDI_WHITE = (0, 2, 4, 5, 7, 9, 11)  # pitch classes from C
LOWEST, HIGHEST = 53, 76  # F3 to E5, the keyboard at rest
HIDDEN = 1024

parts = ['<g id="scales_panel">', '<rect x="0" y="20" width="480" height="200" fill="#000000"/>']


def rect(x, y, w, h, fill):
    parts.append(f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" fill="{fill}" stroke="#000000" stroke-width="1"/>')


def key(identity, d, x, y, fill):
    parts.append(f'<path id="{identity}" transform="matrix(1 0 0 1 {x:g} {y:g})" d="{d}" fill="{fill}" stroke="#000000" stroke-width="1"/>')


def dot(identity, x, y, radius, fill):
    parts.append(f'<circle id="{identity}" transform="matrix(1 0 0 1 {x:g} {y:g})" cx="0" cy="0" r="{radius:g}" fill="{fill}" stroke="{fill}" stroke-width="0.5"/>')


def white_index(note):
    """White keys from C-1 up to this note; a black key counts the white key below it."""
    return note // 12 * 7 + sum(1 for w in MIDI_WHITE if w <= note % 12) - 1


def is_black(note):
    return note % 12 not in MIDI_WHITE


width = 444 / 14
for octave in range(2):
    for i in range(7):
        rect(18 + (octave * 7 + i) * width, 40, width, 56, '#F0F0F0')
    for i in BLACK_AFTER:
        rect(18 + (octave * 7 + i - 0.3) * width, 40, width * 0.6, 34, '#000000')

for k in range(24):
    position, octave = k % 12, k // 12
    black = position in BLACK
    slot = BLACK_AFTER[BLACK.index(position)] if black else WHITE.index(position) + 0.5
    dot(f'scales_dot_{k}', 18 + (octave * 7 + slot) * width, 66 if black else 86, 4, '#F0F0F0' if black else '#000000')

parts.append('<text id="scales_text" transform="matrix(1 0 0 1 240 142)" fill="#F2F2F2" font-family="UniversNextPro-Light" font-size="22">scales</text>')

# The SDK moves and scales every key and activity dot each frame. They are
# authored at rest for the default two octaves, the rest out of view.
def rest_x(note):
    if LOWEST <= note <= HIGHEST:
        return 18 + (white_index(note) - white_index(LOWEST)) * width
    return HIDDEN


for note in range(128):
    if not is_black(note):
        key(f'scales_key_{note}', 'M0 0H10V44H0Z', rest_x(note), 174, '#F0F0F0')
for note in range(128):
    if is_black(note):
        x = rest_x(note)
        key(f'scales_key_{note}', 'M0 0H6V27H0Z', x + width * 0.7 if x != HIDDEN else x, 174, '#000000')
for note in range(128):
    if is_black(note):
        dot(f'scales_played_{note}', HIDDEN, 195, 2.5, '#F0F0F0')
    else:
        dot(f'scales_played_{note}', HIDDEN, 211, 3, '#000000')

parts.append('</g>')
with open(Path(__file__).with_name('panel.svg.fragment'), 'w', encoding='utf-8', newline='\n') as out:
    out.write('\n'.join(parts) + '\n')
