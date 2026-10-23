#!/usr/bin/env python3
"""Check redistributed embedded font bytes and required attribution inputs."""
from pathlib import Path
import re
root = Path(__file__).resolve().parent.parent
cpp = (root / 'fxsound/JuceLibraryCode/BinaryData.cpp').read_text()
header = (root / 'fxsound/JuceLibraryCode/BinaryData.h').read_text()
for index, weight in enumerate(('Bold', 'Regular', 'SemiBold')):
    binary = (root / f'fxsound/Fonts/MontserratAlternates-{weight}.ttf').read_bytes()
    array = re.search(rf'temp_binary_data_{index}\[\] =\s*\{{(.*?)\}};', cpp, re.S)
    assert array, weight
    embedded = bytes(int(value) for value in re.findall(r'\d+', array[1]))
    assert embedded == binary + b'\0\0', f'{weight}: embedded bytes differ'
    symbol = f'MontserratAlternates{weight}_ttf'
    assert f'{symbol}Size = {len(binary)};' in header
    resource_hash = 0
    for character in symbol:
        resource_hash = (31 * resource_hash + ord(character)) & 0xffffffff
    assert f'case 0x{resource_hash:08x}:  numBytes = {len(binary)}; return {symbol};' in cpp
for path in ('LICENSE', 'CREDITS.md', 'MODIFICATIONS.md', 'THIRD_PARTY_NOTICES.md',
             'LICENSES/GPL-3.0.txt', 'LICENSES/MontserratAlternates-OFL.txt',
             'LICENSES/NotoCJK-OFL.txt', 'LICENSES/NotoThai-OFL.txt', 'LICENSES/NotoArabic-OFL.txt'):
    assert (root / path).is_file(), path
assert not list((root / 'fxsound/Fonts').glob('*Gilroy*'))
assert 'Gilroy' not in cpp and 'Gilroy' not in header
print('PASS: open-font embedding, declared sizes, required license and credit inputs')
