// AMD FidelityFX Super Resolution 1 - EASU (edge adaptive spatial upsampling), one pass, for Dolphin for Game Vault.
// Game Vault's "Sharper picture (FSR)" option turns it on for a session (fsr=1 in the start link); its strength
// (fsrs=<percent>) is GV_SHARP, written to Dolphin.ini [gv_fsr-options] by DolphinWinRT/main.cpp.
//
// Ported from libretro/slang-shaders edge-smoothing/fsr/shaders/fsr-pass0.slang (Unlicense), which inlines the
// EASU filter of AMD's ffx_fsr1.h: Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
// copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
// Approximate reciprocals of the original are exact here (1/x, inversesqrt).

/*
[configuration]
[OptionRangeFloat]
GUIName = Sharpness
OptionName = GV_SHARP
MinValue = 0.0
MaxValue = 1.0
StepAmount = 0.1
DefaultValue = 0.5
[/configuration]
*/

float3 GvTexel(float2 sp, float2 off)
{
  return SampleLocation((sp + off + float2(0.5, 0.5)) * GetInvResolution()).rgb;
}

float GvLuma(float3 c)
{
  return c.b * 0.5 + (c.r * 0.5 + c.g);
}

void main()
{
  // the input pixel under this output pixel, and where in it we are
  float2 pp = GetCoordinates() * GetResolution() - float2(0.5, 0.5);
  float2 fp = floor(pp);
  pp -= fp;

  //    b c
  //  e f g h
  //  i j k l
  //    n o
  float3 b = GvTexel(fp, float2(0.0, -1.0));
  float3 c = GvTexel(fp, float2(1.0, -1.0));
  float3 e = GvTexel(fp, float2(-1.0, 0.0));
  float3 f = GvTexel(fp, float2(0.0, 0.0));
  float3 g = GvTexel(fp, float2(1.0, 0.0));
  float3 h = GvTexel(fp, float2(2.0, 0.0));
  float3 i = GvTexel(fp, float2(-1.0, 1.0));
  float3 j = GvTexel(fp, float2(0.0, 1.0));
  float3 k = GvTexel(fp, float2(1.0, 1.0));
  float3 l = GvTexel(fp, float2(2.0, 1.0));
  float3 n = GvTexel(fp, float2(0.0, 2.0));
  float3 o = GvTexel(fp, float2(1.0, 2.0));

  float bL = GvLuma(b), cL = GvLuma(c), eL = GvLuma(e), fL = GvLuma(f), gL = GvLuma(g), hL = GvLuma(h);
  float iL = GvLuma(i), jL = GvLuma(j), kL = GvLuma(k), lL = GvLuma(l), nL = GvLuma(n), oL = GvLuma(o);

  // direction and length of the edge, from the four bilinear quadrants
  float2 dir = float2(0.0, 0.0);
  float len = 0.0;
  float w, lenX, lenY, dirX, dirY;

  w = (1.0 - pp.x) * (1.0 - pp.y);
  lenX = 1.0 / max(max(abs(gL - fL), abs(fL - eL)), 1.0e-6); dirX = gL - eL; dir.x += dirX * w;
  lenX = clamp(abs(dirX) * lenX, 0.0, 1.0); len += lenX * lenX * w;
  lenY = 1.0 / max(max(abs(jL - fL), abs(fL - bL)), 1.0e-6); dirY = jL - bL; dir.y += dirY * w;
  lenY = clamp(abs(dirY) * lenY, 0.0, 1.0); len += lenY * lenY * w;

  w = pp.x * (1.0 - pp.y);
  lenX = 1.0 / max(max(abs(hL - gL), abs(gL - fL)), 1.0e-6); dirX = hL - fL; dir.x += dirX * w;
  lenX = clamp(abs(dirX) * lenX, 0.0, 1.0); len += lenX * lenX * w;
  lenY = 1.0 / max(max(abs(kL - gL), abs(gL - cL)), 1.0e-6); dirY = kL - cL; dir.y += dirY * w;
  lenY = clamp(abs(dirY) * lenY, 0.0, 1.0); len += lenY * lenY * w;

  w = (1.0 - pp.x) * pp.y;
  lenX = 1.0 / max(max(abs(kL - jL), abs(jL - iL)), 1.0e-6); dirX = kL - iL; dir.x += dirX * w;
  lenX = clamp(abs(dirX) * lenX, 0.0, 1.0); len += lenX * lenX * w;
  lenY = 1.0 / max(max(abs(nL - jL), abs(jL - fL)), 1.0e-6); dirY = nL - fL; dir.y += dirY * w;
  lenY = clamp(abs(dirY) * lenY, 0.0, 1.0); len += lenY * lenY * w;

  w = pp.x * pp.y;
  lenX = 1.0 / max(max(abs(lL - kL), abs(kL - jL)), 1.0e-6); dirX = lL - jL; dir.x += dirX * w;
  lenX = clamp(abs(dirX) * lenX, 0.0, 1.0); len += lenX * lenX * w;
  lenY = 1.0 / max(max(abs(oL - kL), abs(kL - gL)), 1.0e-6); dirY = oL - gL; dir.y += dirY * w;
  lenY = clamp(abs(dirY) * lenY, 0.0, 1.0); len += lenY * lenY * w;

  // normalise the direction
  float dirR = dir.x * dir.x + dir.y * dir.y;
  bool zro = dirR < (1.0 / 32768.0);
  dirR = zro ? 1.0 : inversesqrt(dirR);
  dir.x = zro ? 1.0 : dir.x;
  dir *= dirR;

  // shape the kernel
  len = len * 0.5;
  len *= len;
  float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
  float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
  float clp = 1.0 / lob;

  float3 min4 = min(min(f, g), min(j, k));
  float3 max4 = max(max(f, g), max(j, k));

  float3 aC = float3(0.0, 0.0, 0.0);
  float aW = 0.0;
  float2 offs[12] = float2[12](float2(0.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(0.0, 1.0),
                               float2(0.0, 0.0), float2(-1.0, 0.0), float2(1.0, 1.0), float2(2.0, 1.0),
                               float2(2.0, 0.0), float2(1.0, 0.0), float2(1.0, 2.0), float2(0.0, 2.0));
  float3 cols[12] = float3[12](b, c, i, j, f, e, k, l, h, g, o, n);
  for (int t = 0; t < 12; t++)
  {
    float2 d = offs[t] - pp;
    float2 v = float2(d.x * dir.x + d.y * dir.y, d.x * (-dir.y) + d.y * dir.x) * len2;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = (2.0 / 5.0) * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
    float tw = wB * wA;
    aC += cols[t] * tw;
    aW += tw;
  }

  // de-ring: never outside the four nearest input pixels
  float3 pix = min(max4, max(min4, aC / aW));

  // sharpen (Game Vault's FSR strength, 0..1): push away from the plain bilinear blend of the four nearest input
  // pixels, kept inside their range so edges get no halo (the same limit as AMD's CAS/RCAS)
  float3 bil = mix(mix(f, g, pp.x), mix(j, k, pp.x), pp.y);
  pix = min(max4, max(min4, pix + 2.0 * GetOption(GV_SHARP) * (pix - bil)));
  SetOutput(float4(pix, 1.0));
}
