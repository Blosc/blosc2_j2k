# blosc2_j2k

`blosc2_j2k` is an experimental Blosc2 codec plugin for regular JPEG2000
Part 1 / J2K codestreams.

It is a standalone split-out version of the J2K part of the previous
`blosc2_grok` runtime-backend prototype.  The goal is to keep the Blosc2 codec
small and backend-agnostic: the Blosc2 codec is called `j2k`, and the actual
JPEG2000 implementation is selected through backend plugins installed inside
the package.

The package uses the J2K codec id prepared for the c-blosc2 registry:

```text
codec name:     j2k
codec id:       39
library:        libblosc2_j2k.so
Python package: blosc2_j2k
```

The codec is plugin-only.  There is no hidden native fallback in this package.
Backend selection comes from explicit configuration, environment variables, or
the installed manifest.

The default manifest is:

```json
{
  "j2k": ["kakadu", "grok"]
}
```

This means: use Kakadu when it is installed and loadable, otherwise use the
open-source Grok backend.

## Plugin Philosophy

The package follows the same philosophy as the `blosc2_grok` prototype, but
with a narrower responsibility:

- `blosc2_j2k` owns only the Blosc2 codec id and the J2K dispatch logic.
- Each codec backend lives in a backend plugin directory.
- The core library does not directly depend on Kakadu.
- The backend ABI is a small C interface, so new J2K backends can be added
  without changing the Blosc2-facing codec.
- Backend discovery is deterministic and inspectable.
- Python is convenient for configuration and diagnostics, but not required for
  C++/HDF5/web-service runtime when the bootstrap preload path is used.

The installed backend layout is:

```text
blosc2_j2k/
  libblosc2_j2k.so
  libblosc2_jpeg2000_bootstrap.so
  blosc2_j2k_plugins.json
  plugins/
    j2k/
      grok/
        libblosc2_j2k_backend.so
      kakadu/
        libblosc2_kakadu_j2k_backend.so
```

Backend capabilities:

| Backend | Built when | J2K | `uint8` | `uint16` | `uint32` | Redistributable |
| --- | --- | --- | --- | --- | --- | --- |
| `plugins/j2k/grok` | always | yes | yes | J2K only | no | yes |
| `plugins/j2k/kakadu` | Kakadu found | yes | yes | yes | yes | no |

Kakadu is optional and is not redistributed by this project.

## Hands-On Quick Start

For now this plugin depends on the experimental `c-blosc2` and
`python-blosc2` branches that define and expose the proposed J2K/HTJ2K codec
IDs.  The quick start is therefore a full-stack install, but it follows the
same packaging model as the normal `python-blosc2` wheel: the experimental
`python-blosc2` branch bundles the matching experimental `c-blosc2` runtime in
`blosc2/lib`.

Once these branches are merged and released upstream, the `python-blosc2`
clone/build step collapses back to a normal released `blosc2` dependency.

```bash
set -x
set -euo pipefail

mkdir blosc2_j2k_quickstart
cd blosc2_j2k_quickstart

python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip setuptools wheel
python -m pip install scikit-build-core cython numpy
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-20}"

git clone https://github.com/alemirone/python-blosc2.git
git -C python-blosc2 checkout add_j2k_htj2k_custom_codecs
python -m pip install -v --no-build-isolation ./python-blosc2

python - <<'PY'
import blosc2
print("blosc2:", blosc2.__version__)
print("J2K codec:", blosc2.Codec.J2K)
print("HTJ2K codec:", blosc2.Codec.HTJ2K)
PY

git clone --recursive https://github.com/alemirone/blosc2_j2k.git
python -m pip install -v --no-build-isolation ./blosc2_j2k
```

Run the quick example:

```bash
python ./blosc2_j2k/examples/quickstart.py --backend grok
```

The quick example should report codec id `39`, backend `grok`, and exact
lossless equality.

The same example can use float quantization:

```bash
python ./blosc2_j2k/examples/quickstart.py --backend grok --float-mode uint16
```

and can exercise lossy rate mode:

```bash
python ./blosc2_j2k/examples/quickstart.py --backend grok --lossy --codec-meta 80
```

The script creates a deterministic `uint16` image, compresses it with codec id
`39`, decodes it, and prints compression and error metrics.  In lossless mode
it checks exact equality.  In lossy mode it checks that the decoded image is not
bit-identical but remains within bounded error.

Minimal Python usage:

```python
import blosc2
import blosc2_j2k
import numpy as np

blosc2_j2k.register_codec()
blosc2_j2k.configure(backend="grok")

data = (np.arange(128 * 128, dtype=np.uint16).reshape(128, 128) % 4096)
cparams = {
    "codec": blosc2_j2k.CODEC_ID,
    "filters": [],
    "splitmode": blosc2.SplitMode.NEVER_SPLIT,
}

array = blosc2.asarray(data, chunks=data.shape, blocks=data.shape, cparams=cparams)
np.testing.assert_array_equal(array[...], data)
```

Lossy mode uses `codec_meta`:

```python
cparams["codec_meta"] = 80  # interpreted as 8.0 in rate mode
```

## Float32 Quantization Mode

`float32` input is supported only when explicitly enabled.  The codec first
maps each float chunk to an integer image (`uint8`, `uint16`, or `uint32`),
compresses that integer image with the selected J2K backend, and stores a small
private header with the scale metadata.  Decode restores `float32` bytes.

Integer `uint8`, `uint16`, and `uint32` inputs keep the normal path and are not
affected by this mode.

Recommended starting point:

```python
import blosc2_j2k

blosc2_j2k.register_codec()
blosc2_j2k.configure(backend="grok", float_mode="uint16")
```

With optional scale guards:

```python
blosc2_j2k.configure(
    backend="grok",
    float_mode="uint16",
    float_clamp_min=-1.0,
    float_clamp_max=1.0,
)
```

The clamp values do not force every chunk to use the full
`[float_clamp_min, float_clamp_max]` range.  They act as guards on the
per-chunk scale estimation.  For each chunk, the codec first computes the finite
raw minimum and maximum.  If clamp bounds are configured, the effective scale is
then limited by those bounds; conceptually:

```text
scale_min = max(raw_min, float_clamp_min)  # when a lower clamp is set
scale_max = min(raw_max, float_clamp_max)  # when an upper clamp is set
```

If the chunk range is already inside the clamp interval, the scale remains free
inside that interval and is still derived from the chunk data.  Only values that
fall outside the effective scale are saturated before quantization.  This is
intended to prevent anomalous values, for example divergent points produced by
an iterative algorithm, from expanding the quantization scale for the whole
chunk.

Environment-only configuration, useful for HDF5/C++/service deployments:

```bash
export BLOSC2_J2K_FLOAT=uint16
export BLOSC2_J2K_FLOAT_CLAMP_MIN=-1.0
export BLOSC2_J2K_FLOAT_CLAMP_MAX=1.0
export BLOSC2_J2K_FLOAT_NAN_POLICY=fail
```

Accepted `BLOSC2_J2K_FLOAT` values are `off`, `8`, `16`, `32`, `uint8`,
`uint16`, and `uint32`.  `uint16` is the recommended default.  `uint32`
requires a backend that supports 32-bit samples, currently Kakadu.

For lossless inner JPEG2000 compression, the expected quantization error is:

```text
max_abs_error <= (scale_max - scale_min) / (2 * qmax) + float32_margin
```

where `qmax` is `255`, `65535`, or `4294967295`, and `scale_min` /
`scale_max` are the effective per-chunk scale values after the optional clamp
guards.  NaN and Inf values fail clearly in this v1 mode; masks are not
preserved.

Hands-on float examples:

```bash
python examples/quickstart.py --backend grok --float-mode uint16
python examples/quickstart.py --backend grok --float-mode uint8 --float-clamp-min -1 --float-clamp-max 1
```

## Installation

When published as a wheel:

```bash
pip install blosc2-j2k -U
```

For local development:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-20}"
python -m pip install -v --no-build-isolation --force-reinstall --no-deps .
```

If Kakadu is available:

```bash
export KAKADU_ROOT=/path/to/kakadu
export KAKADU_INCLUDE_DIR="$KAKADU_ROOT/managed/all_includes"
export KAKADU_LIB_PATH="$KAKADU_ROOT/lib"
export LD_LIBRARY_PATH="$KAKADU_LIB_PATH:${LD_LIBRARY_PATH:-}"
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-20}"

CMAKE_ARGS="-DKAKADU_ROOT=$KAKADU_ROOT -DKAKADU_INCLUDE_DIR=$KAKADU_INCLUDE_DIR -DKAKADU_LIBRARY_DIR=$KAKADU_LIB_PATH" \
  python -m pip install -v --no-build-isolation --force-reinstall .
```

On macOS, use `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`.  On Windows,
add the directory containing dependent DLLs to `PATH` before starting Python or
the host application.

## Runtime Backend Configuration

The preferred model is explicit configuration before the first encode/decode or
HDF5 read/write:

```python
import blosc2_j2k

blosc2_j2k.configure(backend="grok")
```

All arguments are optional.  If `plugin_path` is omitted, the runtime searches
the default plugin root installed next to `libblosc2_j2k`:

```text
<libblosc2_j2k directory>/plugins
```

Named backend discovery resolves:

```text
${plugin_root}/j2k/${backend}
```

For example:

```text
<libblosc2_j2k directory>/plugins/j2k/grok
```

Selection priority is:

1. Explicit API configuration: `blosc2_j2k.configure()` or
   `blosc2_j2k_configure()`.
2. Legacy direct-directory variable: `BLOSC2_J2K_REPLACEMENT_DIR`.
3. Named backend variables: `BLOSC2_J2K_PLUGIN_PATH` and
   `BLOSC2_J2K_BACKEND`.
4. Installed manifest `blosc2_j2k_plugins.json`.

Configuration is finalized on first codec use.  Later attempts to reconfigure
fail clearly.

Environment examples:

```bash
export BLOSC2_J2K_BACKEND=grok
```

or, for a custom plugin root:

```bash
export BLOSC2_J2K_PLUGIN_PATH=/opt/blosc2_j2k/plugins
export BLOSC2_J2K_BACKEND=grok
```

Legacy direct-directory example:

```bash
export BLOSC2_J2K_REPLACEMENT_DIR=/opt/blosc2_j2k/plugins/j2k/grok
```

## Diagnostics

From Python:

```python
import blosc2_j2k

print(blosc2_j2k.available_backends())
print(blosc2_j2k.list_plugins())
print(blosc2_j2k.diagnose())
print(blosc2_j2k.selftest())
```

From the command line:

```bash
python -m blosc2_j2k --list-plugins
python -m blosc2_j2k --diagnose
python -m blosc2_j2k --selftest
```

The diagnostic JSON reports plugin roots, manifest priority, selected backend,
resolved float configuration, loadability, ABI validity, dependent-library
loader errors, and relevant environment variables.

## Compression Parameters

For compatibility with the original `blosc2_grok` interface, the Python module
still exposes `set_params_defaults(...)` and the Grok-style parameter names.
The most commonly useful control for quick tests is `codec_meta`.

If `codec_meta` is zero or omitted, compression is lossless.  If it is non-zero,
rate mode is activated with:

```text
rate = codec_meta / 10.0
```

Example:

```python
cparams = {
    "codec": blosc2_j2k.CODEC_ID,
    "codec_meta": 5 * 10,  # rate value 5.0
    "filters": [],
    "splitmode": blosc2.SplitMode.NEVER_SPLIT,
}
```

Only rates below `25.6` are supported through this one-byte notation.

## HDF5 And Loader Order

When reading or writing through HDF5, the HDF5 Blosc2 filter must also be
discoverable:

```bash
export HDF5_PLUGIN_PATH="$(python -c 'import hdf5plugin; print(hdf5plugin.PLUGIN_PATH)')"
```

For Python processes that use the Python API directly, importing `blosc2_j2k`
loads the codec library with global visibility and checks that the active
c-blosc2 build knows the J2K codec id:

```python
import hdf5plugin
import blosc2_j2k
import h5py

blosc2_j2k.configure(backend="grok")
```

This is enough for Python code that calls the `blosc2_j2k` registry check
before compression/decompression.  It is not always enough for a fully
transparent HDF5-only path, because the HDF5 Blosc2 filter may use a separate
Blosc2 library instance.  In that case the filter can find Blosc2 and the HDF5
plugin, but the process may still need the codec library to be loaded before
the first HDF5 callback.

`LD_LIBRARY_PATH` only makes shared libraries discoverable.  `HDF5_PLUGIN_PATH`
only makes the HDF5 Blosc2 filter discoverable.  With the official codec id,
c-blosc2 must already contain the J2K registry entry.  `LD_PRELOAD` does not
replace that registry support; it is a loader-order helper for applications
that cannot import `blosc2_j2k` or call `blosc2_j2k_register_codec()` before
HDF5 starts using Blosc2.

Transparent HDF5 example with `h5py` and `hdf5plugin`:

```bash
export J2K_PACKAGE=/path/to/site-packages/blosc2_j2k
export BLOSC2_PACKAGE=/path/to/site-packages/blosc2

export HDF5_PLUGIN_PATH=/path/to/hdf5plugin/plugins
export LD_LIBRARY_PATH="${J2K_PACKAGE}:${BLOSC2_PACKAGE}/lib:${LD_LIBRARY_PATH:-}"
export LD_PRELOAD="${J2K_PACKAGE}/libblosc2_jpeg2000_bootstrap.so${LD_PRELOAD:+:${LD_PRELOAD}}"

export BLOSC2_J2K_BACKEND=grok

python my_hdf5_reader_or_writer.py
```

If the Kakadu backend is selected, add the Kakadu library directory as well:

```bash
export LD_LIBRARY_PATH=/path/to/kakadu/lib:${LD_LIBRARY_PATH}
export BLOSC2_J2K_BACKEND=kakadu
```

For C/C++ applications, the explicit path is:

```c
#include "blosc2_j2k_public.h"

blosc2_init();
blosc2_j2k_register_codec();

blosc2_j2k_runtime_config cfg = {0};
cfg.struct_size = sizeof(cfg);
cfg.backend = "grok";

if (blosc2_j2k_configure(&cfg) != 0) {
    fprintf(stderr, "%s\n", blosc2_j2k_last_error());
}
```

For unmodified C++ programs, HDF5 command-line tools, web servers, or web
clients that cannot call this API, preload the bootstrap library:

```bash
export HDF5_PLUGIN_PATH=/path/to/hdf5/plugins
export LD_LIBRARY_PATH=/path/to/blosc2/lib:/path/to/blosc2_j2k:${LD_LIBRARY_PATH:-}
export LD_PRELOAD=/path/to/blosc2_j2k/libblosc2_jpeg2000_bootstrap.so${LD_PRELOAD:+:${LD_PRELOAD}}
```

The bootstrap calls `blosc2_init()` early so the c-blosc2 registry is populated
before the first HDF5 callback.  The expected registry entries are:

```text
j2k   -> 39
htj2k -> 40
```

This is a deployment bridge for loader-order issues in HDF5-only or service
runtimes.  It assumes a c-blosc2 build that includes the JPEG2000 ids in the
built-in registry.  Older temporary-id files need the previous temporary-id
branch, and older c-blosc2 builds that do not know id `39` must be updated; the
public Blosc2 user-codec API cannot add global ids below the user range.

## Current Tests

The current tests cover:

- manifest and plugin listing;
- codec registry check with id `39`;
- lossless J2K roundtrip;
- lossy J2K roundtrip through the Grok backend;
- optional Kakadu `uint32` lossless roundtrip when Kakadu is installed;
- command-line plugin listing.

## Limitations

- Codec id `39` requires a c-blosc2 build that knows the J2K registry entry.
  The bootstrap helps with loader order, but it cannot add a new global id to an
  older c-blosc2 build through the public user-codec API.
- Kakadu is optional and not redistributable.
- The Grok backend handles the normal J2K path up to `uint16`; the optional
  Kakadu backend also supports `uint32`.
- For Kakadu `uint32`, the default wavelet decomposition is capped at
  `Clevels=3` for robust small-chunk operation. Advanced users can override it
  with `BLOSC2_J2K_CLEVELS` or `BLOSC2_J2K_KAKADU_PARAMS`.
- The minimum practical image payload is around 256 bytes.

## Next Steps Toward An Official Plugin

1. Publish this standalone repository as the candidate `blosc2_j2k` plugin.
2. Validate Python, C/C++, HDF5, and service-runtime usage.
3. Merge the c-blosc2 registry PR that adds `BLOSC_CODEC_J2K = 39`.
4. Keep the bootstrap only as a loader-order helper for HDF5-only deployments.
5. Keep the backend ABI independent from the Blosc2-facing codec.
6. Keep Grok as the open-source backend and Kakadu as an optional external
   backend.

## More Examples

See the [examples](examples/) directory.

## Thanks

Thanks to Francesc Alted for the guidance on the Blosc2 plugin direction,
including the suggestion to split J2K and HTJ2K into separate codecs.  This work
builds on the original `blosc2_grok` plugin and on the Grok JPEG2000 library.
