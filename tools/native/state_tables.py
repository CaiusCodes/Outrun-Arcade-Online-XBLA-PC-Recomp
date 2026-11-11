"""Name Perfect Dark's render-state and sampler-state get/set functions from D3D's
own tables in .data: 101 render-state entries and 20 sampler-state entries of
{Get, Set, Default}, as in GoldenEye (refs/d3d-naming-tools/tables.py). The tables
are found from setters the code matcher already placed (CullMode, MagFilter).

Usage: python -I state_tables_pd.py <decrypted image> <cull setter hex> <magfilter getter hex>
Prints TOML lines. The image is game content: keep it out of repos and releases.
"""
import struct
import sys

RS = ['ZEnable', 'ZFunc', 'ZWriteEnable', 'FillMode', 'CullMode', 'AlphaBlendEnable', 'SeparateAlphaBlendEnable',
      'BlendFactor', 'SrcBlend', 'DestBlend', 'BlendOp', 'SrcBlendAlpha', 'DestBlendAlpha', 'BlendOpAlpha',
      'AlphaTestEnable', 'AlphaRef', 'AlphaFunc', 'StencilEnable', 'TwoSidedStencilMode', 'StencilFail',
      'StencilZFail', 'StencilPass', 'StencilFunc', 'StencilRef', 'StencilMask', 'StencilWriteMask',
      'CCWStencilFail', 'CCWStencilZFail', 'CCWStencilPass', 'CCWStencilFunc', 'CCWStencilRef', 'CCWStencilMask',
      'CCWStencilWriteMask', 'ClipPlaneEnable', 'PointSize', 'PointSizeMin', 'PointSpriteEnable', 'PointSizeMax',
      'MultiSampleAntiAlias', 'MultiSampleMask', 'ScissorTestEnable', 'SlopeScaleDepthBias', 'DepthBias',
      'ColorWriteEnable', 'ColorWriteEnable1', 'ColorWriteEnable2', 'ColorWriteEnable3', 'TessellationMode',
      'MinTessellationLevel', 'MaxTessellationLevel'] + ['Wrap%d' % i for i in range(16)] + [
      'ViewportEnable', 'HighPrecisionBlendEnable', 'HighPrecisionBlendEnable1', 'HighPrecisionBlendEnable2',
      'HighPrecisionBlendEnable3', 'HalfPixelOffset', 'PrimitiveResetEnable', 'PrimitiveResetIndex',
      'AlphaToMaskEnable', 'AlphaToMaskOffsets', 'GuardBand_X', 'GuardBand_Y', 'DiscardBand_X', 'DiscardBand_Y',
      'HiStencilEnable', 'HiStencilWriteEnable', 'HiStencilFunc', 'HiStencilRef', 'PresentInterval',
      'PresentImmediateThreshold', 'HiZEnable', 'HiZWriteEnable', 'LastPixel', 'LineWidth', 'Buffer2Frames']
SS = ['AddressU', 'AddressV', 'AddressW', 'BorderColor', 'MagFilter', 'MinFilter', 'MipFilter', 'MipMapLodBias',
      'MaxMipLevel', 'MaxAnisotropy', 'MagFilterZ', 'MinFilterZ', 'SeparateZFilterEnable', 'MinMipLevel',
      'TrilinearThreshold', 'AnisotropyBias', 'HGradientExpBias', 'VGradientExpBias', 'WhiteBorderColorW',
      'PointBorderEnable']

img = open(sys.argv[1], 'rb').read()
cull_set = int(sys.argv[2], 16)
mag_get = int(sys.argv[3], 16)


def find_all(value):
    pat = struct.pack('>I', value)
    out, i = [], img.find(pat)
    while i >= 0:
        out.append(i)
        i = img.find(pat, i + 1)
    return out


def entry(base, i):
    o = base + i * 12 - 0x82000000
    return struct.unpack('>3I', img[o:o + 12])


# CullMode is render-state entry 14 (index 4 after the 10 unused stubs): {get, SET, default}.
rs_base = None
for off in find_all(cull_set):
    base = 0x82000000 + off - 4 - 14 * 12
    g, s, d = entry(base, 14)
    if s == cull_set and d == 6:  # D3DCULL_CCW on the 360
        rs_base = base
ss_base = None
for off in find_all(mag_get):
    base = 0x82000000 + off - 4 * 12
    g, s, d = entry(base, 4)
    if g == mag_get:
        ss_base = base
print('# render-state table 0x%08X, sampler-state table 0x%08X' % (rs_base or 0, ss_base or 0))
seen = {}
for i, nm in enumerate(RS):
    g, s, d = entry(rs_base, i + 10)
    for addr, kind in ((s, 'Set'), (g, 'Get')):
        name = 'D3DDevice_%sRenderState_%s' % (kind, nm)
        if addr in seen:
            print('# 0x%08X also %s (shared with %s)' % (addr, name, seen[addr]))
            continue
        seen[addr] = name
        print('0x%08X = { name = "%s" }  # table: D3DRS %d, default 0x%08X' % (addr, name, (i + 10) * 4, d))
for i, nm in enumerate(SS):
    g, s, d = entry(ss_base, i)
    for addr, kind in ((s, 'Set'), (g, 'Get')):
        name = 'D3DDevice_%sSamplerState_%s' % (kind, nm)
        if addr in seen:
            print('# 0x%08X also %s (shared with %s)' % (addr, name, seen[addr]))
            continue
        seen[addr] = name
        print('0x%08X = { name = "%s" }  # table: D3DSAMP %d, default 0x%X' % (addr, name, i * 4, d))
print('# %d table entries' % len(seen))
