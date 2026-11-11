"""Extract actual patched SDK methods for a differential packet-dispatch test."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root/'out/renderer-race-bulk-20260920/base_command_processor.cpp').read_text(encoding='utf-8')
methods = []
for name in ('WriteRegistersFromMem', 'WriteRegisterRangeFromRing', 'ExecutePacketType0'):
    signature = ('bool' if name == 'ExecutePacketType0' else 'void') + ' CommandProcessor::' + name + '('
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[begin:end])
(root/'out/renderer-register-test-methods.inc').write_text('\n\n'.join(methods), encoding='utf-8')
print('Extracted actual patched packet/range methods for differential test')
