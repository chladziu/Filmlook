# -*- coding: utf-8 -*-
"""Grain plates for Film Look (v0.13): ML/DATA/FILMLOOK.GRN

A plate is a tile of film grain made here, on the PC, and read by the camera: 256 x 128 texels, three signed bytes (R, G, B)
per texel, tiling without a seam.  Several plates (variants) per class of film: the camera takes one of them at random for
every photo and walks it with a shift / mirror of its own in every cell of the frame, so two photos never show the same grain and
no pattern repeats inside one photo.

What a class changes (these are the things that make Portra look different from Tri-X or Velvia):
  size      the plate is made for grain "6 px wide" (the lattice spacing of the old grain); the camera walks it faster to get the
            smaller grain of a fine film or of a smaller picture, so the plates only differ in character, not in size
  clumping  silver grain (B&W) clumps: positive skew and a longer tail, dye clouds (colour) are rounder and softer
  rough     how much of the grain is the fine layer (0.625 of the coarse spacing) next to the coarse one
  colour    colour film: the three dye layers grain independently.  `rho` is the share of the grain the three channels have in
            common (the luma grain); the rest differs from channel to channel.  `gain` is the size of the grain in R, G and B
            (the blue layer of a colour negative is the noisiest, the red one the finest)
  mono      B&W: R = G = B

The plates are normalised so that the LUMA grain has rms 32 in every class: the amount of grain of a film (GrainAmount, from the
manufacturers' RMS figures, see grain_targets.py) means the same thing whatever the class.

File: "FLGR", u16 version (1), u16 classes, u16 variants, u16 width, u16 height, u16 reserved  (16 bytes, little endian)
      then classes x variants plates of width x height x 3 bytes, class after class.
"""
import struct
import sys
import numpy as np

W, H = 256, 128
VARIANTS = 6
RMS = 32.0

# name, rho (shared share of the 3 channels), gains R,G,B, skew (clumping), rough (fine layer weight), mono
CLASSES = [
    ("neg",       0.55, (0.85, 1.00, 1.25), 0.22, 0.55, False),   # colour negative: soft dye clouds, blue the noisiest
    ("cine",      0.45, (0.85, 1.00, 1.30), 0.26, 0.65, False),   # Vision3: more colour in the grain, a bit rougher
    ("slide",     0.70, (0.90, 1.00, 1.10), 0.15, 0.45, False),   # slide film: tight, little colour noise
    ("bw_cubic",  1.00, (1.0, 1.0, 1.0),    0.30, 0.85, True),    # Tri-X, HP5: silver grain, clumpy and irregular
    ("bw_tab",    1.00, (1.0, 1.0, 1.0),    0.20, 0.40, True),    # Delta (T-grain): even, no big clumps
    ("bw_chromo", 1.00, (1.0, 1.0, 1.0),    0.18, 0.45, True),    # XP2 Super (dye image): soft, like a colour negative
]
NOMINAL = 6.0     # grain "width" of the plate, px (the lattice spacing of the old value-noise grain)


def freq_grid():
    fy = np.fft.fftfreq(H)[:, None]
    fx = np.fft.fftfreq(W)[None, :]
    return fx, fy


def lowpass(rng, size_px):
    """Gaussian noise with a Gaussian spectrum; size_px is the lattice spacing of a value noise of the same width."""
    fx, fy = freq_grid()
    sig = size_px * 0.55                         # autocorrelation width matched to the smoothstep value noise (see check_width)
    k2 = (2 * np.pi) ** 2 * (fx ** 2 + fy ** 2)
    filt = np.exp(-0.5 * k2 * sig ** 2 / 2.0)
    z = np.fft.ifft2(np.fft.fft2(rng.standard_normal((H, W))) * filt).real
    return z / z.std()


def field(rng, size_px, rough, skew):
    z = lowpass(rng, size_px) + rough * lowpass(rng, size_px * 0.625)
    z = z / z.std()
    if skew > 0:                                 # clumps: dark/light grains stand out from a calmer background
        z = np.exp(skew * z)                      # log-normal: skew is the sigma of the logarithm
    z = z - z.mean()
    return z / z.std()


def plate(cls, variant):
    name, rho, gains, skew, rough, mono = cls
    rng = np.random.default_rng(1000 * (1 + [c[0] for c in CLASSES].index(name)) + variant)
    L = field(rng, NOMINAL, rough, skew)
    if mono:
        ch = np.stack([L, L, L], axis=-1)
    else:
        U = [field(rng, NOMINAL, rough, skew) for _ in range(3)]
        ch = np.stack([np.sqrt(rho) * L + np.sqrt(1 - rho) * U[i] for i in range(3)], axis=-1)
        ch = ch * np.array(gains)[None, None, :]
    luma = 0.299 * ch[..., 0] + 0.587 * ch[..., 1] + 0.114 * ch[..., 2]
    ch = ch / luma.std() * RMS
    return np.clip(np.round(ch), -127, 127).astype(np.int8)


def check_width():
    """autocorrelation of the plate noise 1 and 2 pixels apart when it is walked at 2 texels per pixel. Measured on rendered
    pictures, a plate walked at 1.96 texels per pixel (Portra) has the autocorrelation of the old grain with the lattice
    spacing 3.06 px (0.77 / 0.39 at 1 and 2 px), and the same for Tri-X, Velvia and Delta 3200, so a film keeps the size it had"""
    rng = np.random.default_rng(1)
    z = lowpass(rng, NOMINAL)
    z = z[:, ::2]                                # a walk of 2 texels per pixel (about 1.96)
    f = np.fft.fft(z, axis=1)
    ac = np.fft.ifft(f * np.conj(f), axis=1).real.mean(axis=0)
    ac = ac / ac[0]
    return ac[1], ac[2]


def main(out):
    data = bytearray(struct.pack('<4sHHHHHH', b'FLGR', 1, len(CLASSES), VARIANTS, W, H, 0))
    for cls in CLASSES:
        for v in range(VARIANTS):
            p = plate(cls, v)
            data += p.tobytes()
    open(out, 'wb').write(bytes(data))
    print(out, len(data), 'bytes,', len(CLASSES), 'classes x', VARIANTS, 'variants')
    print('autocorrelation of the plate walked 2 texels per pixel, 1 and 2 px apart: %.2f %.2f (coarse layer alone; with the fine layer, measured on rendered pictures: 0.75 0.37, the old grain 0.77 0.39)' % check_width())


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'FILMLOOK.GRN')
