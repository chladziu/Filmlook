/* fd_engine.c - the film engine of the module (modules/filmlook/engine: Soup Engine, GPL v2) as one object, listed exactly like
 * the module lists it (engine_unit.c), so that a new engine file added there is picked up here without any change. */
#ifndef FD_WASM
#  ifndef SE_HOSTED
#    define SE_HOSTED 1
#  endif
#endif
#include "../../modules/filmlook/engine_unit.c"
