"""Read/write the @ByteArray() blobs QSettings writes into an INI.

Mirrors QSettingsPrivate::iniEscapedString / iniUnescapedString. The subtle
part is escapeNextIfDigit: after a \\0 or a \\xNN escape, a following hex
digit is escaped too, so the minimal-width \\x escapes stay unambiguous.
"""
import re

_SIMPLE = {'a': 7, 'b': 8, 'f': 12, 'n': 10, 'r': 13, 't': 9, 'v': 11,
           '"': 34, '\\': 92, '0': 0}
_HEX = set('0123456789abcdefABCDEF')


def unescape(s):
    out = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c != '\\':
            out.append(ord(c)); i += 1; continue
        i += 1
        n = s[i]
        if n == 'x':
            i += 1
            j = i
            while j < len(s) and s[j] in _HEX:
                j += 1
            out.append(int(s[i:j], 16) & 0xFF)
            i = j
        elif n in _SIMPLE:
            out.append(_SIMPLE[n]); i += 1
        else:
            out.append(ord(n)); i += 1
    return bytes(out)


def escape(b):
    out = []
    esc_next_if_digit = False
    for ch in b:
        if esc_next_if_digit and chr(ch) in _HEX:
            out.append('\\x%x' % ch)
            continue
        esc_next_if_digit = False
        if ch == 0:
            out.append('\\0'); esc_next_if_digit = True
        elif ch in (7, 8, 12, 10, 13, 9, 11):
            out.append('\\' + 'abfnrtv'[(7, 8, 12, 10, 13, 9, 11).index(ch)])
        elif ch in (34, 92):
            out.append('\\' + chr(ch))
        elif ch <= 0x1F or ch >= 0x7F:
            out.append('\\x%x' % ch); esc_next_if_digit = True
        else:
            out.append(chr(ch))
    return ''.join(out)


def read_key(text, key):
    m = re.search(r'^' + re.escape(key) + r'="?@ByteArray\((.*)\)"?$', text, re.M)
    assert m, 'key %s not found' % key
    return m.group(1), m.span(1)
