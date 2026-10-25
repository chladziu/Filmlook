/* engine_unit.c - the JPG engine (the files in engine/, derived from Soup Engine, GPL v2) as one object of the module.
 * It is compiled on its own so that Magic Lantern's headers (and their macros) never see it: it only needs the
 * handful of libc functions declared in engine/se_libc.h, which the camera provides. */
#include "engine/se.c"
#include "engine/jpeg_dec.c"
#include "engine/jpeg_enc.c"
#include "engine/look.c"
