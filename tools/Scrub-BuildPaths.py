"""Neutralise build-machine folders in shipped binaries (same length, in place).

Debug strings (__FILE__ in asserts and logs) of binaries built without
-ffile-prefix-map name the folder they were built in, e.g. a Windows user
folder. Each NUL-terminated "X:/Users/..." string keeps its length and its tail
(linkers share string tails); only the part before the project's own folders
(rexglue-sdk*, src, include, thirdparty, out) becomes "./" filler.

    python -I tools/Scrub-BuildPaths.py <file>...   -> exit 1 if a user path is left
"""
import re
import sys

PATH = re.compile(rb'[A-Za-z]:[\\/]Users[\\/][^\x00]*')
ROOTS = re.compile(rb'[\\/](rexglue-sdk[^\\/\x00]*|src|include|thirdparty|out)[\\/]', re.I)


def filler(n):
    s = ('./' * n)[:n]
    return (s[:-1] + '/').encode() if n else b''


def scrub(data):
    count = 0
    for m in list(PATH.finditer(bytes(data))):
        text = m.group(0)
        root = ROOTS.search(text)
        cut = root.start() + 1 if root else text.replace(b'\\', b'/').rfind(b'/') + 1
        if cut <= 0:
            continue
        data[m.start():m.start() + cut] = filler(cut)
        count += 1
    return count


def main(paths):
    bad = 0
    for path in paths:
        data = bytearray(open(path, 'rb').read())
        count = scrub(data)
        left = len(re.findall(rb'[A-Za-z]:[\\/]Users[\\/]', bytes(data)))
        open(path, 'wb').write(data)
        print(f'{path}: {count} build paths neutralised, {left} left')
        bad += left
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
