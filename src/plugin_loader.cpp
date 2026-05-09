/*********************************************************************
 * blosc2_grok: runtime replacement plugin discovery and loading.
 *
 * This file owns dynamic-loader state.  It deliberately does not know how to
 * encode or decode: callers receive a validated family-specific ABI descriptor.
 *
 * Copyright (c) 2026  The Blosc Development Team <blosc@blosc.org>
 * https://blosc.org
 * License: GNU Affero General Public License v3.0 (see LICENSE.txt)
 *********************************************************************/

#include "plugin_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>  // LoadLibraryA/GetProcAddress/FreeLibrary.
#else
#include <dlfcn.h>  // dlopen/dlsym/dlclose for backend shared libraries.
#endif

namespace blosc2_grok_detail {
namespace {

namespace fs = std::filesystem;

#if defined(_WIN32)
using plugin_library_handle_t = HMODULE;
#else
using plugin_library_handle_t = void*;
#endif

// Runtime replacement backend state.  Each codec family has an independent
// descriptor pointer and dynamic-loader handle.
//
// Two pointers are needed for each family because they have different roles:
// the plugin pointer is the callable ABI object used by the codec, while the
// handle is the dynamic-loader ownership token kept for dlclose()/FreeLibrary().
// Keeping only the ABI pointer would not give us a safe way to unload the
// library, and closing the handle too early would invalidate the ABI pointer.
static j2k_codec_plugin_t* s_j2k_replacement_plugin = nullptr;
static plugin_library_handle_t s_j2k_plugin_handle = nullptr;
static htj2k_codec_plugin_t* s_htj2k_replacement_plugin = nullptr;
static plugin_library_handle_t s_htj2k_plugin_handle = nullptr;

// Return the process-wide lock protecting lazy replacement backend loading.
std::mutex &replacement_plugin_mutex() {
    static std::mutex plugin_mutex;
    return plugin_mutex;
}

// Return whether a filesystem entry looks like a loadable backend library.
bool has_plugin_library_extension(const fs::path &path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".so" || ext == ".dylib" || ext == ".dll";
}

#if defined(_WIN32)
// Format the last Windows dynamic-loader error for debug output.
std::string last_windows_loader_error() {
    DWORD error_code = GetLastError();
    if (error_code == 0) {
        return "unknown loader error";
    }

    LPSTR message = nullptr;
    DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, error_code, 0, reinterpret_cast<LPSTR>(&message), 0, nullptr);
    if (size == 0 || message == nullptr) {
        return "Windows error " + std::to_string(error_code);
    }

    std::string result(message, size);
    LocalFree(message);
    return result;
}
#endif

// Open a backend shared library with the platform dynamic loader.
plugin_library_handle_t open_plugin_library(const fs::path &libpath, bool debug) {
    std::string path = libpath.string();
#if defined(_WIN32)
    plugin_library_handle_t handle = LoadLibraryA(path.c_str());
    if (!handle && debug) {
        fprintf(stderr, "[blosc2_grok] LoadLibrary failed for %s: %s\n",
                path.c_str(), last_windows_loader_error().c_str());
    }
    return handle;
#else
    plugin_library_handle_t handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle && debug) {
        fprintf(stderr, "[blosc2_grok] dlopen failed for %s: %s\n", path.c_str(), dlerror());
    }
    return handle;
#endif
}

// Close a backend shared library handle.
void close_plugin_library(plugin_library_handle_t handle) {
    if (!handle) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

// Resolve the exported J2K plugin descriptor from an open backend library.
j2k_codec_plugin_t* find_j2k_plugin_descriptor(plugin_library_handle_t handle,
                                               const fs::path &libpath,
                                               bool debug) {
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(handle, J2K_CODEC_PLUGIN_SYMBOL);
    if (!symbol) {
        if (debug) {
            std::string path = libpath.string();
            fprintf(stderr, "[blosc2_grok] GetProcAddress failed for %s: %s\n",
                    path.c_str(), last_windows_loader_error().c_str());
        }
        return nullptr;
    }
    return reinterpret_cast<j2k_codec_plugin_t*>(symbol);
#else
    dlerror();
    void *symbol = dlsym(handle, J2K_CODEC_PLUGIN_SYMBOL);
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
        if (debug) {
            std::string path = libpath.string();
            fprintf(stderr, "[blosc2_grok] dlsym failed for %s: %s\n", path.c_str(), dlsym_error);
        }
        return nullptr;
    }
    return reinterpret_cast<j2k_codec_plugin_t*>(symbol);
#endif
}

// Resolve the exported HTJ2K plugin descriptor from an open backend library.
htj2k_codec_plugin_t* find_htj2k_plugin_descriptor(plugin_library_handle_t handle,
                                                   const fs::path &libpath,
                                                   bool debug) {
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress(handle, HTJ2K_CODEC_PLUGIN_SYMBOL);
    if (!symbol) {
        if (debug) {
            std::string path = libpath.string();
            fprintf(stderr, "[blosc2_grok] GetProcAddress failed for %s: %s\n",
                    path.c_str(), last_windows_loader_error().c_str());
        }
        return nullptr;
    }
    return reinterpret_cast<htj2k_codec_plugin_t*>(symbol);
#else
    dlerror();
    void *symbol = dlsym(handle, HTJ2K_CODEC_PLUGIN_SYMBOL);
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
        if (debug) {
            std::string path = libpath.string();
            fprintf(stderr, "[blosc2_grok] dlsym failed for %s: %s\n", path.c_str(), dlsym_error);
        }
        return nullptr;
    }
    return reinterpret_cast<htj2k_codec_plugin_t*>(symbol);
#endif
}

// Validate the J2K plugin ABI before using names, versions or function pointers.
bool is_valid_j2k_plugin_descriptor(const j2k_codec_plugin_t *plugin,
                                    const fs::path &libpath,
                                    bool debug) {
    std::string path = libpath.string();
    if (!plugin) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] Missing J2K plugin descriptor in %s\n", path.c_str());
        }
        return false;
    }
    if (plugin->abi_version != J2K_CODEC_PLUGIN_ABI_VERSION) {
        if (debug) {
            fprintf(stderr,
                    "[blosc2_grok] J2K plugin ABI mismatch in %s: got %u, expected %u\n",
                    path.c_str(), plugin->abi_version, J2K_CODEC_PLUGIN_ABI_VERSION);
        }
        return false;
    }
    if (plugin->struct_size < sizeof(j2k_codec_plugin_t)) {
        if (debug) {
            fprintf(stderr,
                    "[blosc2_grok] Plugin descriptor too small in %s: got %u, expected at least %zu\n",
                    path.c_str(), plugin->struct_size, sizeof(j2k_codec_plugin_t));
        }
        return false;
    }
    if (!plugin->name || !plugin->version || !plugin->vtable.supports ||
        !plugin->vtable.encode || !plugin->vtable.decode) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] Incomplete J2K plugin descriptor in %s\n", path.c_str());
        }
        return false;
    }
    return true;
}

// Validate the HTJ2K plugin ABI before using names, versions or function pointers.
bool is_valid_htj2k_plugin_descriptor(const htj2k_codec_plugin_t *plugin,
                                      const fs::path &libpath,
                                      bool debug) {
    std::string path = libpath.string();
    if (!plugin) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] Missing HTJ2K plugin descriptor in %s\n", path.c_str());
        }
        return false;
    }
    if (plugin->abi_version != HTJ2K_CODEC_PLUGIN_ABI_VERSION) {
        if (debug) {
            fprintf(stderr,
                    "[blosc2_grok] HTJ2K plugin ABI mismatch in %s: got %u, expected %u\n",
                    path.c_str(), plugin->abi_version, HTJ2K_CODEC_PLUGIN_ABI_VERSION);
        }
        return false;
    }
    if (plugin->struct_size < sizeof(htj2k_codec_plugin_t)) {
        if (debug) {
            fprintf(stderr,
                    "[blosc2_grok] HTJ2K plugin descriptor too small in %s: got %u, expected at least %zu\n",
                    path.c_str(), plugin->struct_size, sizeof(htj2k_codec_plugin_t));
        }
        return false;
    }
    if (!plugin->name || !plugin->version || !plugin->vtable.supports ||
        !plugin->vtable.encode || !plugin->vtable.decode) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] Incomplete HTJ2K plugin descriptor in %s\n", path.c_str());
        }
        return false;
    }
    return true;
}

}  // namespace

j2k_codec_plugin_t* load_j2k_replacement_plugin() {
    std::lock_guard<std::mutex> lock(replacement_plugin_mutex());
    if (s_j2k_replacement_plugin) {
        return s_j2k_replacement_plugin;
    }

    const char* replacement_dir = getenv("BLOSC2_GROK_REPLACEMENT_DIR");
    if (!replacement_dir || replacement_dir[0] == '\0') {
        return nullptr;
    }

    bool debug = (getenv("BLOSC2_GROK_DEBUG") != nullptr);
    fs::path replacement_path(replacement_dir);
    std::error_code ec;
    if (!fs::is_directory(replacement_path, ec)) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] J2K replacement directory is not readable: %s\n",
                    replacement_dir);
        }
        return nullptr;
    }

    fs::directory_iterator it(replacement_path, ec);
    fs::directory_iterator end;
    if (ec) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] Could not scan replacement directory %s: %s\n",
                    replacement_dir, ec.message().c_str());
        }
        return nullptr;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            if (debug) {
                fprintf(stderr, "[blosc2_grok] Error while scanning %s: %s\n",
                        replacement_dir, ec.message().c_str());
            }
            break;
        }

        fs::path libpath = it->path();
        if (!has_plugin_library_extension(libpath)) {
            continue;
        }

        plugin_library_handle_t handle = open_plugin_library(libpath, debug);
        if (!handle) {
            continue;
        }

        j2k_codec_plugin_t *plugin = find_j2k_plugin_descriptor(handle, libpath, debug);
        if (is_valid_j2k_plugin_descriptor(plugin, libpath, debug)) {
            s_j2k_plugin_handle = handle;
            s_j2k_replacement_plugin = plugin;
            if (debug) {
                std::string path = libpath.string();
                fprintf(stderr, "[blosc2_grok] Loaded J2K plugin: %s %s from %s\n",
                        plugin->name, plugin->version, path.c_str());
            }
            return plugin;
        }

        close_plugin_library(handle);
    }

    if (debug) {
        fprintf(stderr, "[blosc2_grok] No valid J2K plugin found in %s\n", replacement_dir);
    }
    return nullptr;
}

htj2k_codec_plugin_t* load_htj2k_replacement_plugin() {
    std::lock_guard<std::mutex> lock(replacement_plugin_mutex());
    if (s_htj2k_replacement_plugin) {
        return s_htj2k_replacement_plugin;
    }

    const char* replacement_dir = getenv("BLOSC2_GROK_HTJ2K_REPLACEMENT_DIR");
    if (!replacement_dir || replacement_dir[0] == '\0') {
        return nullptr;
    }

    bool debug = (getenv("BLOSC2_GROK_DEBUG") != nullptr);
    fs::path replacement_path(replacement_dir);
    std::error_code ec;
    if (!fs::is_directory(replacement_path, ec)) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] HTJ2K replacement directory is not readable: %s\n",
                    replacement_dir);
        }
        return nullptr;
    }

    fs::directory_iterator it(replacement_path, ec);
    fs::directory_iterator end;
    if (ec) {
        if (debug) {
            fprintf(stderr, "[blosc2_grok] Could not scan HTJ2K replacement directory %s: %s\n",
                    replacement_dir, ec.message().c_str());
        }
        return nullptr;
    }

    for (; it != end; it.increment(ec)) {
        if (ec) {
            if (debug) {
                fprintf(stderr, "[blosc2_grok] Error while scanning %s: %s\n",
                        replacement_dir, ec.message().c_str());
            }
            break;
        }

        fs::path libpath = it->path();
        if (!has_plugin_library_extension(libpath)) {
            continue;
        }

        plugin_library_handle_t handle = open_plugin_library(libpath, debug);
        if (!handle) {
            continue;
        }

        htj2k_codec_plugin_t *plugin = find_htj2k_plugin_descriptor(handle, libpath, debug);
        if (is_valid_htj2k_plugin_descriptor(plugin, libpath, debug)) {
            s_htj2k_plugin_handle = handle;
            s_htj2k_replacement_plugin = plugin;
            if (debug) {
                std::string path = libpath.string();
                fprintf(stderr, "[blosc2_grok] Loaded HTJ2K plugin: %s %s from %s\n",
                        plugin->name, plugin->version, path.c_str());
            }
            return plugin;
        }

        close_plugin_library(handle);
    }

    if (debug) {
        fprintf(stderr, "[blosc2_grok] No valid HTJ2K plugin found in %s\n", replacement_dir);
    }
    return nullptr;
}

void unload_replacement_plugins() {
    std::lock_guard<std::mutex> lock(replacement_plugin_mutex());
    if (s_j2k_plugin_handle) {
        close_plugin_library(s_j2k_plugin_handle);
        s_j2k_plugin_handle = nullptr;
    }
    if (s_htj2k_plugin_handle) {
        close_plugin_library(s_htj2k_plugin_handle);
        s_htj2k_plugin_handle = nullptr;
    }
    s_j2k_replacement_plugin = nullptr;
    s_htj2k_replacement_plugin = nullptr;
}

}  // namespace blosc2_grok_detail
