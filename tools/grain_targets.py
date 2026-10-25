# -*- coding: utf-8 -*-
"""Grain targets for the films, taken from the manufacturers' figures instead of from the presets.

Value = "visible grain" on one scale for all films (about the RMS granularity x1000 of a colour negative, i.e. Fuji
Superia 400 = 4.4, Kodak Portra 400 = 4.4 (PGI 41-43 x 0.105), Kodak Tri-X = 8).  The generator turns it into the
GrainAmount of the preset with GAIN, which was chosen so that Portra 400 keeps the amount it always had (28).

How the figures were converted (the sources do not share one scale, so every family has its own factor):
  colour negative, Fuji / Agfa / Konica "RMS" (4 = ISO 100..400, 5 = ISO 800)         x 1.1
  colour negative, Kodak "PGI" (Royal Gold 400 = 41, Gold 200 = 47, Max/Pro 1000 = 57)  x 0.105
  slide film (Kodak 10..19, Fuji 8..22 "RMS", not comparable with negative film)        x 0.22
  B&W negative (Kodak T-Max 100 = 8, T-Max 400 = 10, P3200 = 18, APX 100 = 9, APX 400 = 14) x 0.5
Source tags: C = number from the pasted grain catalogue / data sheet, E = estimate (the manufacturer gives no number:
only "fine" / "very fine"), V = variant of another entry (format, development, expiry).
"""
GAIN = 6.4                      # GrainAmount per unit

T = {
 # ---- Kodak colour negative
 "Kodak ColorPlus 200": (4.7, "E"), "Kodak Gold 200": (4.9, "C"), "Kodak Gold 200 Frontier": (4.9, "V"),
 "Kodak Gold 200 Noritsu": (5.0, "V"), "Kodak Gold 200 Expired 10yr": (6.2, "V"), "Kodak Gold 200 Expired 30yr": (8.0, "V"),
 "Kodak Farbwelt 200 Expired": (5.4, "V"), "Kodak High Definition Plus 200": (4.6, "C"), "Kodak Kodacolor VR 200": (5.0, "E"),
 "Kodak Max 800": (6.0, "C"), "Kodak Ektar 100": (2.6, "E"),
 "Kodak Portra 160": (3.3, "C"), "Kodak Portra 160NC": (3.2, "C"), "Kodak Portra 160VC": (3.5, "C"),
 "Kodak Portra 400": (4.4, "C"), "Kodak Portra 400 4x5": (1.8, "V"), "Kodak Portra 400 120": (2.9, "V"),
 "Kodak Portra 400 Expired": (6.2, "V"), "Kodak Portra 400 Noritsu": (4.6, "V"), "Kodak Portra 400NC": (4.3, "C"),
 "Kodak Portra 400UC": (4.4, "E"), "Kodak Portra 400VC": (4.5, "C"), "Kodak Portra 800": (5.4, "E"),
 "Kodak Pro Image 100": (3.4, "E"), "Kodak Royal Gold 400": (4.3, "C"), "Kodak Ultramax 400": (4.7, "E"),
 "Kodak Vericolor III 160": (3.4, "E"), "Kodak Vericolor VPS": (3.4, "E"),
 "Kodak Vision3 50D": (2.4, "E"), "Kodak Vision3 250D": (3.8, "E"), "Kodak Vision3 250D ECN-2": (4.0, "E"),
 "Kodak Vision3 500T": (4.6, "E"), "Kodak Vision3 500T ECN-2": (5.0, "E"),
 # ---- Fuji colour negative and the digital simulations
 "Fuji 100 Industrial": (4.4, "E"), "Fujicolor C200": (4.4, "E"), "Fujicolor NPH": (4.4, "C"), "Fuji Natura 1600": (7.0, "E"),
 "Fuji NPS 160 Pulled": (3.6, "V"), "Fuji Pro 160C": (3.8, "C"), "Fuji Pro 160NS": (3.6, "C"), "Fuji Pro 400H": (4.4, "C"),
 "Fuji Pro 800Z": (5.6, "E"), "Fuji Reala 100": (3.7, "E"), "Fuji Superia 100": (4.4, "C"), "Fuji Superia 200": (4.4, "C"),
 "Fuji Superia 800": (5.5, "C"), "Fuji Superia 1600": (7.5, "E"), "Fuji Superia HG 1600": (7.0, "E"),
 "Fuji Superia Premium 400": (4.2, "E"), "Fuji Superia X-TRA 400": (4.4, "C"),
 "Fuji Classic Chrome": (3.6, "E"), "Fuji Classic Negative": (4.8, "E"), "Fuji Eterna": (3.6, "E"), "Fuji Nostalgic Negative": (4.0, "E"),
 # ---- Agfa and the rest
 "Agfa Optima 100": (4.4, "E"), "Agfa Optima 200": (4.8, "E"), "Agfa Ultra 100": (5.0, "C"), "Agfa Vista 100": (4.4, "C"),
 "Agfa Vista 200": (4.95, "C"), "Agfa Vista 400": (4.95, "C"),
 "Adox Color Implosion": (5.5, "E"), "Rollei Crossbird 200": (4.9, "E"), "CineStill 50D": (2.4, "E"), "CineStill 400D": (4.0, "E"),
 "Lomography Color 100": (4.0, "E"), "Lomography Lomochrome Purple": (5.6, "E"), "Ferrania Solaris FG 400": (5.6, "E"),
 "ORWO NC 500": (6.2, "E"), "C Print": (4.0, "E"), "RA-4 Color Print": (3.4, "E"), "SantaColor Aerocolor": (4.5, "E"),
 # ---- B&W
 "Kodak Panatomic-X 32": (2.5, "E"), "Kodak Plus-X 125": (4.5, "E"), "Kodak Recording Film 2475": (11.0, "E"),
 "Kodak T-Max 100": (4.0, "C"), "Kodak T-Max 400": (5.0, "C"), "Kodak T-Max 3200": (9.0, "C"), "Kodak Technical Pan": (1.5, "C"),
 "Kodak Tri-X 400": (8.0, "E"), "Kodak Tri-X 400 120": (6.0, "V"), "Kodak Tri-X 400 Expired": (11.0, "V"),
 "Kodak Tri-X 400 HC-110": (8.8, "V"), "Kodak Tri-X 400 Rodinal": (10.0, "V"), "Kodak Verichrome Pan": (4.5, "E"),
 "Ilford Delta 100": (3.5, "E"), "Ilford Delta 400": (5.0, "E"), "Ilford Delta 3200": (8.5, "E"), "Ilford Delta 3200 Microphen": (9.5, "V"),
 "Ilford FP4 Plus 125": (4.5, "E"), "Ilford HP5 Plus": (7.0, "E"), "Ilford HP5 Plus DD-X": (6.2, "V"),
 "Ilford HP5 Plus Expired": (9.5, "V"), "Ilford HP5 Plus Stand Dev": (7.5, "V"), "Ilford Ortho Plus 80": (3.8, "E"),
 "Ilford Pan F Plus 50": (3.8, "E"), "Ilford SFX 200": (5.5, "E"), "Ilford XP2 Super": (4.2, "E"), "Ilford MG Warmtone": (4.5, "E"),
 "Fuji Acros": (3.2, "E"), "Fuji Neopan 400": (6.0, "E"), "Fuji Neopan 1600": (9.5, "E"),
 "Agfa APX 25": (3.5, "C"), "Agfa APX 100": (4.5, "C"), "Agfa APX 400": (7.0, "C"), "Agfa Scala 200": (3.5, "C"),
 "Fomapan 100": (4.5, "E"), "Fomapan 200": (5.5, "E"), "Fomapan 400": (7.0, "E"), "Adox Silvermax 21": (1.8, "E"),
 "Rollei Retro 80S": (3.5, "E"), "Rollei Retro 400S": (6.5, "E"), "Kentmere Pan 400": (7.0, "E"), "Bergger Pancro 400": (6.5, "E"),
 "Copper Tone": (5.0, "E"), "Gold Tone": (5.0, "E"), "Selenium Tone": (5.0, "E"), "Sepia Tone": (5.0, "E"), "Split Warm-Cool": (5.0, "E"),
 "Dramatic Monochrome": (8.0, "E"), "M645 Medium Format": (3.5, "E"), "Ricoh High-Contrast": (7.0, "E"),
 # ---- slides
 "Kodak Ektachrome 64": (2.6, "C"), "Kodak Ektachrome 100G": (2.2, "C"), "Kodak Ektachrome 100VS": (2.4, "C"),
 "Kodak Ektachrome 200": (2.6, "C"), "Kodak Ektachrome 320T": (4.2, "C"), "Kodak Ektachrome 400X": (4.2, "C"),
 "Kodak Ektachrome E100": (2.0, "C"), "Kodak Elite Chrome 200": (2.6, "C"), "Kodak Elite Chrome 400": (3.5, "E"),
 "Kodak Kodachrome 25": (2.4, "C"), "Kodak Kodachrome 64": (2.6, "C"), "Kodak Kodachrome 200": (4.2, "C"),
 "Fuji Astia 100F": (2.2, "C"), "Fuji Fortia SP 50": (2.0, "C"), "Fuji Provia 100F": (1.8, "C"), "Fuji Provia 400F": (3.3, "C"),
 "Fuji Provia 400X": (3.3, "C"), "Fuji Sensia 100": (2.2, "C"), "Fuji Velvia 50": (2.0, "C"), "Fuji Velvia 100": (1.9, "E"),
 "AgfaChrome RS 100": (2.2, "C"),
}
