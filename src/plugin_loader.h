/*********************************************************************
 * blosc2_grok: runtime replacement plugin discovery and loading.
 *
 * Responsibilities:
 * - read the family-specific replacement environment variables;
 * - scan one directory for loadable backend shared libraries;
 * - isolate dlopen/dlsym/dlclose and LoadLibrary/GetProcAddress/FreeLibrary;
 * - validate ABI/version/struct size before returning plugin descriptors;
 * - keep plugin discovery idempotent under concurrent initialization.
 *
 * Copyright (c) 2026  The Blosc Development Team <blosc@blosc.org>
 * https://blosc.org
 * License: GNU Affero General Public License v3.0 (see LICENSE.txt)
 *********************************************************************/

#ifndef BLOSC2_GROK_PLUGIN_LOADER_H
#define BLOSC2_GROK_PLUGIN_LOADER_H

#include "htj2k_codec_api.h"
#include "j2k_codec_api.h"

namespace blosc2_grok_detail {

// Load and cache the first valid J2K backend found in BLOSC2_GROK_REPLACEMENT_DIR.
j2k_codec_plugin_t *load_j2k_replacement_plugin();

// Load and cache the first valid HTJ2K backend found in BLOSC2_GROK_HTJ2K_REPLACEMENT_DIR.
htj2k_codec_plugin_t *load_htj2k_replacement_plugin();

// Release all loaded replacement backends.  Called from blosc2_grok_destroy().
void unload_replacement_plugins();

}  // namespace blosc2_grok_detail

#endif  // BLOSC2_GROK_PLUGIN_LOADER_H
