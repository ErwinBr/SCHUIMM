/*
 * SCHUIMM: the single translation unit that compiles miniaudio's implementation. The feature
 * and backend selection (MA_NO_*, MA_ENABLE_*) is set as public compile definitions of the
 * schuimm_miniaudio target, so every file that includes miniaudio.h sees the same
 * configuration.
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
