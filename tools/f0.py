#!/usr/bin/env python3
"""The metal table, computed rather than remembered.

Each metal's normal-incidence reflectance F0 in linear sRGB, from published complex
refractive indices:

    F0(lambda) = ( (n - 1)^2 + k^2 ) / ( (n + 1)^2 + k^2 )

integrated against the CIE 1931 2-degree colour-matching functions under illuminant
D65, normalised so that a perfect reflector is Y = 1, and taken from XYZ to linear
sRGB (D65 white) with the IEC 61966-2-1 matrix. No gamma: the table is linear.

Data, all in tools/f0-data/ so this runs offline and the numbers can be re-derived
from what is committed:

  Cu, Ag, Au   P. B. Johnson and R. W. Christy, "Optical constants of the noble
               metals", Phys. Rev. B 6, 4370 (1972)
  Fe           P. B. Johnson and R. W. Christy, "Optical constants of transition
               metals: Ti, V, Cr, Mn, Fe, Co, Ni, and Pd", Phys. Rev. B 9, 5056 (1974)
  brass        M. R. Querry, "Optical constants", Contractor Report CRDC-CR-85034
               (1985), the 70% Cu / 30% Zn alloy (cartridge brass, C260)
  all five as tabulated in the refractiveindex.info database (CC0 1.0):
               https://refractiveindex.info  (files under database/data/main/<M>/nk/
               and database/data/other/alloys/Cu-Zn/nk/)
  CMFs, D65    CIE 1931 2-degree observer at 5 nm and the CIE D65 relative spectral
               power distribution at 1 nm, as published by CVRL (cvrl.org).

Iron is bulk polycrystalline iron; the plugin's "Steel" is that. Gold's red comes out
above 1 (its F0 lies outside the sRGB gamut), and the shipped table clamps it to 1, as
every published table does; the raw value is printed beside it. Tabulated n,k are
interpolated linearly in wavelength between the measured points, which sit 5-20 nm
apart across the visible for Johnson & Christy and 10 nm apart for Querry.

    python3 tools/f0.py           # print the table
    python3 tools/f0.py --check   # exit 1 if source/Controls.cpp disagrees at 3 dp
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "f0-data"

METALS = [
    ("Copper", "Cu-Johnson.yml"),
    ("Brass", "CuZn-Querry-Cu70Zn30.yml"),
    ("Silver", "Ag-Johnson.yml"),
    ("Gold", "Au-Johnson.yml"),
    ("Steel", "Fe-Johnson.yml"),
]

# XYZ (D65) -> linear sRGB, IEC 61966-2-1.
M = (
    (3.2406, -1.5372, -0.4986),
    (-0.9689, 1.8758, 0.0415),
    (0.0557, -0.2040, 1.0570),
)


def read_nk(path):
    """(wavelength nm, n, k) rows of a refractiveindex.info 'tabulated nk' file."""
    rows = []
    text = path.read_text(encoding="utf-8")
    body = text.split("data: |", 1)[1]
    for line in body.splitlines():
        parts = line.split()
        if len(parts) == 3:
            try:
                um, n, k = (float(p) for p in parts)
            except ValueError:
                continue
            rows.append((um * 1000.0, n, k))
    rows.sort()
    return rows


def read_csv(path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 2 and re.match(r"^\d", parts[0]):
            rows.append(tuple(float(p) for p in parts))
    return rows


def interp(table, x, column):
    """Linear interpolation of column `column` of a sorted (x, ...) table at x."""
    lo, hi = 0, len(table) - 1
    if x <= table[lo][0]:
        return table[lo][column]
    if x >= table[hi][0]:
        return table[hi][column]
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if table[mid][0] <= x:
            lo = mid
        else:
            hi = mid
    x0, x1 = table[lo][0], table[hi][0]
    t = (x - x0) / (x1 - x0)
    return table[lo][column] * (1.0 - t) + table[hi][column] * t


def f0_rgb(nk, cmf, d65):
    """Linear sRGB F0 of a metal whose n,k table is `nk`."""
    lam0, lam1 = nk[0][0], nk[-1][0]
    xyz = [0.0, 0.0, 0.0]
    white = [0.0, 0.0, 0.0]
    for lam in range(360, 831):  # 1 nm, the CMFs interpolated from 5 nm
        s = interp(d65, lam, 1)
        x, y, z = (interp(cmf, lam, c) for c in (1, 2, 3))
        for i, c in enumerate((x, y, z)):
            white[i] += s * c
        if not (lam0 <= lam <= lam1):
            raise SystemExit(f"n,k table does not cover {lam} nm ({lam0:.0f}-{lam1:.0f})")
        n = interp(nk, lam, 1)
        k = interp(nk, lam, 2)
        r = ((n - 1.0) ** 2 + k * k) / ((n + 1.0) ** 2 + k * k)
        for i, c in enumerate((x, y, z)):
            xyz[i] += s * c * r
    xyz = [v / white[1] for v in xyz]
    return tuple(sum(M[i][j] * xyz[j] for j in range(3)) for i in range(3))


def table():
    cmf = read_csv(DATA / "ciexyz31.csv")
    d65 = read_csv(DATA / "d65.csv")
    out = []
    for name, file in METALS:
        out.append((name, f0_rgb(read_nk(DATA / file), cmf, d65)))
    return out


def shipped():
    """The table source/Controls.cpp carries, in order."""
    text = (HERE.parent / "source/Controls.cpp").read_text(encoding="utf-8")
    block = text.split("kF0[ kMetalCount ] = {", 1)[1].split("};", 1)[0]
    return [tuple(float(v) for v in m) for m in re.findall(r"\{\s*([\d.]+),\s*([\d.]+),\s*([\d.]+)\s*\}", block)]


def main(argv):
    rows = table()
    print(f"{'metal':8s} {'R':>7s} {'G':>7s} {'B':>7s}   (linear sRGB, D65, CIE 1931 2 deg)")
    for name, (r, g, b) in rows:
        note = "" if max(r, g, b) <= 1.0 else f"   shipped clamped to 1: {min(r,1):.3f} {min(g,1):.3f} {min(b,1):.3f}"
        print(f"{name:8s} {r:7.3f} {g:7.3f} {b:7.3f}{note}")
    if "--check" in argv:
        have = shipped()
        worst = 0.0
        for (name, want), got in zip(rows, have):
            for w, g in zip(want, got):
                worst = max(worst, abs(round(min(w, 1.0), 3) - g))
        ok = len(have) == len(rows) and worst < 5e-4
        print(f"source/Controls.cpp {'matches' if ok else 'DIFFERS FROM'} the computed table at 3 dp (worst {worst:.1e})")
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
