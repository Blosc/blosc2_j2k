# blosc2_j2k

Temporary Blosc2 plugin package for regular JPEG2000/J2K codestreams.

During the transition before official c-blosc2 codec ids are assigned, this
package registers a local dynamic codec:

```text
codec name: j2k
temporary id: 160
library: libblosc2_j2k.so
```

The codec itself is plugin-only.  The default backend priority is read from
`blosc2_j2k_plugins.json`:

```json
{
  "j2k": ["kakadu", "grok"]
}
```

The `grok` backend is always built and is used as the open-source J2K backend.
The Kakadu backend is built only when Kakadu headers and libraries are provided.

## Installation

For using `blosc2_j2k` you will first have to install its wheel:

```shell
pip install blosc2-j2k -U
```

## Usage

```python
import blosc2
import numpy as np
import blosc2_j2k

blosc2_j2k.configure(backend="grok")

data = np.arange(64 * 64, dtype=np.uint16).reshape(64, 64)
cparams = {
    "codec": blosc2_j2k.CODEC_ID,
    "filters": [],
    "splitmode": blosc2.SplitMode.NEVER_SPLIT,
}
bl_array = blosc2.asarray(
    data,
    chunks=data.shape,
    blocks=data.shape,
    cparams=cparams,
)
np.testing.assert_array_equal(bl_array[...], data)
```

For C/C++ or HDF5-only programs that cannot call Python, preload the bootstrap
library so c-blosc2 registers the temporary ids before the first codec use:

```bash
export HDF5_PLUGIN_PATH=/path/to/hdf5/plugins
export LD_LIBRARY_PATH=/path/to/blosc2/lib:/path/to/blosc2_j2k:${LD_LIBRARY_PATH:-}
export LD_PRELOAD=/path/to/blosc2_j2k/libblosc2_jpeg2000_bootstrap.so

# Optional.  If omitted, the manifest priority chooses kakadu then grok.
export BLOSC2_J2K_BACKEND=grok
```

## Parameters for compression

The following parameters are available for compression for grok, with their defaults.  Most of them are named after the ones in the [Pillow library](https://pillow.readthedocs.io/en/stable/handbook/image-file-formats.html#jpeg-2000-saving) and have the same meaning.  The ones that are not in Pillow are marked with a `*` and you can get more information about them in the [grok documentation](https://github.com/GrokImageCompression/grok/wiki/3.-grk_compress), or by following the provided links.  For those marked with a ``**``, you can get more information in the [grok.h header](https://github.com/GrokImageCompression/grok/blob/a84ac2592e581405a976a00cf9e6f03cab7e2481/src/lib/core/grok.h#L975
).

    'tile_size': (0, 0),
    'tile_offset': (0, 0),
    'quality_mode': None,
    'quality_layers': np.zeros(0, dtype=np.float64),
    'progression': "LRCP",
    'num_resolutions': 6,
    'codeblock_size': (64, 64),
    'irreversible': False,
    'precinct_size': (0, 0),
    'offset': (0, 0),
    'mct': 0,
    * 'numgbits': 2,  # Equivalent to -N, -guard_bits
    * 'roi_compno': -1,  # Together with 'roi_shift' it is equivalent to -R, -ROI
    * 'roi_shift': 0,
    * 'decod_format': GrkFileFmt.GRK_FMT_UNK,
    * 'cod_format': GrkFileFmt.GRK_FMT_UNK,
    * 'rsiz': GrkProfile.GRK_PROFILE_NONE,  # Equivalent to -Z, -rsiz
    * 'framerate': 0,
    * 'apply_icc_': False,  # Equivalent to -f, -apply_icc
    * 'rateControlAlgorithm': GrkRateControl.BISECT,
    * 'num_threads': 0,
    * 'deviceId': 0,  # Equivalent to -G, -device_id
    * 'duration': 0,  # Equivalent to -J, -duration
    * 'repeats': 1,  # Equivalent to -e, -repetitions
    * 'mode': GrkMode.DEFAULT,  # Equivalent to -M, -mode
    * 'verbose': False,  # Equivalent to -v, -verbose
    ** 'enableTilePartGeneration': False,  # See header of grok.h above
    ** 'max_cs_size': 0,  # See header of grok.h above
    ** 'max_comp_size': 0,  # See header of grok.h above

*Note: * when using the `blosc2_j2k` plugin from C, the structure used
for setting the parameters uses the `grok` parameters names. You can see an example
in https://github.com/Blosc/leaps-examples/blob/main/c-compression/compress-tomo.c#L110 .

### codec_meta as rates quality mode

As a simpler way to activate the rates quality mode, if you set the `codec_meta` from the `cparams` to an
integer different from 0, the rates quality mode will be activated with a rate value equal to `codec_meta` / 10. If 
`cod_format` is not specified, the default will be used. The `codec_meta` has priority to the `rates` param set with the 
`blosc2_j2k.set_params_defaults()`. Please note that only rates < 25.6 are supported with this notation.
```python
import blosc2


cparams = {
    'codec': blosc2.Codec.GROK,
    'codec_meta': 5 * 10,  # cratio will be 5
    'filters': [],
    'splitmode': blosc2.SplitMode.NEVER_SPLIT,
}
```

## Runtime backend configuration

`blosc2_j2k` can route J2K and HTJ2K codestreams through family-specific
runtime backends.  The preferred model is explicit configuration before the
first encode/decode operation: Python users call `blosc2_j2k.configure()`, and
C/C++ hosts call `blosc2_j2k_configure()`.  Environment variables are still
supported for command-line tools, HDF5-only deployments, and backwards
compatibility.

Regular J2K has a built-in Grok path and therefore works without any runtime
plugin.  HTJ2K does not use the native Grok path in this package; HTJ2K
encode/decode requires an HTJ2K backend such as OpenHTJ2K or Kakadu.

Runtime plugins are split by codestream family:

* J2K plugins export `J2K_CODEC_PLUGIN`, defined in
  `src/plugin/j2k_codec_api.h`.
* HTJ2K plugins export `HTJ2K_CODEC_PLUGIN`, defined in
  `src/plugin/htj2k_codec_api.h`.

Named backend discovery resolves plugins as:

```text
${plugin_root}/${family}/${backend}
```

`plugin_root` is optional.  If it is not configured, the runtime automatically
uses the default plugin root installed next to the shared library:

```text
<libblosc2_j2k directory>/plugins
```

For example, with the default plugin root, `family=htj2k` and
`backend=openhtj2k` selects:

```text
<libblosc2_j2k directory>/plugins/htj2k/openhtj2k
```

### Python configuration

Use `configure()` before the first Blosc2 encode/decode or HDF5 read/write:

```python
import blosc2_j2k

blosc2_j2k.configure(
    j2k_backend="grok",
    htj2k_backend="openhtj2k",
)
```

All arguments are optional.  `plugin_path` is only needed when plugins are not
installed under the default root next to `libblosc2_j2k`.  A typical HTJ2K-only
configuration can leave J2K untouched:

```python
import blosc2_j2k

blosc2_j2k.configure(htj2k_backend="openhtj2k")
```

The runtime can be inspected from Python:

```python
import blosc2_j2k

print(blosc2_j2k.available_backends())
print(blosc2_j2k.list_plugins())
print(blosc2_j2k.diagnose())
print(blosc2_j2k.selftest())
```

The same diagnostics are available from the command line:

```bash
python -m blosc2_j2k --list-plugins
python -m blosc2_j2k --diagnose
python -m blosc2_j2k --selftest
```

### C/C++ configuration

C/C++ applications that link or explicitly load `libblosc2_j2k` should
configure the runtime before opening HDF5 files or using Blosc2 data that may
need the codec:

```c
#include "blosc2_j2k_public.h"

blosc2_j2k_runtime_config cfg = {0};
cfg.struct_size = sizeof(cfg);
cfg.backend = "grok";

if (blosc2_j2k_configure(&cfg) != 0) {
    fprintf(stderr, "%s\n", blosc2_j2k_last_error());
}
```

`blosc2_j2k_list_plugins()` and `blosc2_j2k_diagnose()` return JSON text into
a caller-provided buffer.  Passing `NULL, 0` returns the required byte count.

### Environment-variable configuration

Environment variables remain useful when the host application cannot call the
configuration API.  If no explicit API call has been made, backend selection is:

1. Legacy direct-directory variable:
   `BLOSC2_J2K_REPLACEMENT_DIR`.
2. Named backend variables:
   `BLOSC2_J2K_PLUGIN_PATH` and `BLOSC2_J2K_BACKEND`.
3. Default plugin root next to the shared library, when a named backend is used
   without `BLOSC2_J2K_PLUGIN_PATH`.
4. Installed manifest `blosc2_j2k_plugins.json`, if present.  The packaged
   manifest prefers Kakadu when the Kakadu plugin is installed, then falls
   back to the Grok plugin: `["kakadu", "grok"]`.

An explicit API call has priority over all backend-selection environment
variables.  Configuration is finalized on first codec use; later calls to
`configure()` or `blosc2_j2k_configure()` fail with a clear error.

Named backend example:

```bash
export BLOSC2_J2K_BACKEND="grok"
```

Set `BLOSC2_J2K_PLUGIN_PATH` only for non-default plugin locations.

Legacy direct-directory examples:

```bash
export BLOSC2_J2K_REPLACEMENT_DIR="/opt/blosc2_j2k/plugins/j2k/grok"
```

### Available plugins

The source tree always builds and installs the Grok J2K backend in
`blosc2_j2k/plugins/j2k/grok`.

If Kakadu headers and libraries are available at build time, CMake builds
`blosc2_j2k/plugins/j2k/kakadu`.
Kakadu discovery can be configured with `KAKADU_ROOT`, `KAKADU_INCLUDE_DIR` and
`KAKADU_LIBRARY_DIR` or `KAKADU_LIB_PATH`.  Kakadu is optional and not
redistributed by this project.

Backend capabilities:

| Backend | Built when | J2K | `uint8` | `uint16` |
| --- | --- | --- | --- | --- |
| `plugins/j2k/grok` | always | yes | yes | J2K only |
| `plugins/j2k/kakadu` | Kakadu found | yes | yes | yes |

### Building optional backends

Kakadu example, when local Kakadu libraries are available:

```bash
export KAKADU_ROOT=/path/to/kakadu
export KAKADU_INCLUDE_DIR="$KAKADU_ROOT/managed/all_includes"
export KAKADU_LIB_PATH="$KAKADU_ROOT/lib"
export LD_LIBRARY_PATH="$KAKADU_LIB_PATH:${LD_LIBRARY_PATH:-}"

CMAKE_ARGS="-DKAKADU_ROOT=$KAKADU_ROOT -DKAKADU_INCLUDE_DIR=$KAKADU_INCLUDE_DIR -DKAKADU_LIBRARY_DIR=$KAKADU_LIB_PATH" \
  pip install -v --no-build-isolation --force-reinstall .
```

On macOS, use `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`.  On Windows,
add the directory containing dependent DLLs to `PATH` before starting Python or
the host application.

### HDF5 and loader order

When reading or writing through HDF5, the HDF5 Blosc2 filter must also be
discoverable:

```bash
export HDF5_PLUGIN_PATH="$(python -c 'import hdf5plugin; print(hdf5plugin.PLUGIN_PATH)')"
```

For Python processes, importing `blosc2_j2k` before the first HDF5 read/write
loads the native codec library with global visibility:

```python
import blosc2_j2k
import hdf5plugin
import h5py
```

For C/C++ applications, the preferred approach is to link or load
`libblosc2_j2k` and call `blosc2_j2k_register_codec()` and
`blosc2_j2k_configure()` before HDF5 starts reading compressed datasets.
`LD_PRELOAD` remains useful for applications or command line tools where no
explicit initialization hook is available:

```bash
export LD_LIBRARY_PATH=/path/to/blosc2/lib:/path/to/blosc2_j2k:${LD_LIBRARY_PATH:-}
export LD_PRELOAD=/path/to/blosc2_j2k/libblosc2_jpeg2000_bootstrap.so
```

The bootstrap calls `blosc2_init()` and registers both temporary ids, `j2k=160`
and `htj2k=161`, after Blosc2 has initialized its built-in registry.

If the dynamic loader reports that a shared object or DLL cannot be opened, the
backend was found but one of its dependent libraries was not.  In practice,
this usually means a backend library directory is missing from
`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH` or `PATH`, depending on the platform.

## Notes

When using `blosc2_j2k`, there are some restrictions that you have
to keep in mind.

* The minimum supported image size is around 256 bytes, so an image with
  less size will fail to be compressed.
* The maximum datatype precision is of 16 bits.
* Although floats from 16 or fewer bits of precision seem to work, we
  recommend using integer data when possible.

## More examples

See the [examples](examples/) directory for more examples.

## Thanks

Thanks to Marta Iborra, from the Blosc Development Team, for doing most of the job in making this plugin possible, and J. David Ibáñez and Francesc Alted for the initial contributions.  Also, thanks to Aaron Boxer, the original author of the [grok library](https://github.com/GrokImageCompression/grok), for his help in ironing out issues for making this interaction possible. 

That's all folks!

The Blosc Development Team
