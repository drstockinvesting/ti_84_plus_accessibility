"""Restore CEmu's window to the one size where the calculator is not distorted.

CEmu scales its two panels by different rules: the screen widget is a FIXED size
(settings.cpp:lcdAdjust -> setFixedSize) pinned to the left, while the keypad is
aspect-locked, centred horizontally and anchored to the TOP
(keypad/keypadwidget.cpp:resizeEvent leaves the y translation at 0). So at any
window size other than the fitted one, the screen sits top-left and the keypad
sits bottom-centre, and the calculator visibly comes apart. This puts it back.

The fitted height is screen + keypad at a common width W:
    screen  = W * 351/440   = 0.7977 * W
    keypad  = W * 238/162   = 1.4691 * W
    content = 2.2668 * W
At [Screen] scale=90 the width is 440*0.90 = 396, so content = 897.
"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cemu_qini as qini

CFG = os.path.expanduser('~/Library/Preferences/cemu-dev/CEmu/cemu_config.ini')
X, TOP_FRAME, TOP_NORM = 2, 33, 61
WIDTH, CONTENT_H = 396, 897

def main():
    t = open(CFG).read()
    raw, span = qini.read_key(t, 'geometry')
    b = bytearray(qini.unescape(raw))
    if len(b) != 66:
        print('unexpected geometry blob (%d bytes); leaving it alone' % len(b), file=sys.stderr)
        return 1
    right, bottom = X + WIDTH - 1, TOP_NORM + CONTENT_H - 1
    struct.pack_into('>iiii', b,  8, X, TOP_FRAME, right, bottom)   # frameGeometry
    struct.pack_into('>iiii', b, 24, X, TOP_NORM,  right, bottom)   # normalGeometry
    struct.pack_into('>iiii', b, 50, X, TOP_NORM,  right, bottom)   # trailing v3 rect
    new = qini.escape(bytes(b))
    if qini.unescape(new) != bytes(b):
        print('geometry round-trip failed; leaving it alone', file=sys.stderr)
        return 1
    open(CFG, 'w').write(t[:span[0]] + new + t[span[1]:])
    print('window size reset to %dx%d' % (WIDTH, CONTENT_H))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
