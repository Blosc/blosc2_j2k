import json
import subprocess
import sys

import blosc2
import blosc2_j2k
import numpy as np


def test_j2k_manifest_and_listing():
    plugins = blosc2_j2k.list_plugins()["plugins"]
    assert any(p["family"] == "j2k" and p["backend"] == "grok" for p in plugins)
    diag = blosc2_j2k.diagnose()
    assert diag["manifest_priority"]["j2k"] == ["kakadu", "grok"]


def test_j2k_roundtrip_with_temporary_codec():
    blosc2_j2k.register_codec()
    blosc2_j2k.configure(backend="grok")
    data = (np.arange(64 * 64, dtype=np.uint16).reshape(64, 64) % 4096)
    cparams = {
        "codec": blosc2_j2k.CODEC_ID,
        "filters": [],
        "splitmode": blosc2.SplitMode.NEVER_SPLIT,
    }
    compressed = blosc2.asarray(data, chunks=data.shape, blocks=data.shape, cparams=cparams)
    np.testing.assert_array_equal(compressed[...], data)


def test_j2k_lossy_roundtrip_with_grok_plugin():
    code = r"""
import json

import blosc2
import blosc2_j2k
import numpy as np

blosc2_j2k.register_codec()
blosc2_j2k.configure(backend="grok")

y, x = np.mgrid[0:128, 0:128]
data = (
    22000
    + 9000 * np.sin(x / 5.0)
    + 7000 * np.cos(y / 9.0)
    + ((x * y) % 2048)
).clip(0, 65535).astype(np.uint16)
cparams = {
    "codec": blosc2_j2k.CODEC_ID,
    "codec_meta": 80,
    "filters": [],
    "splitmode": blosc2.SplitMode.NEVER_SPLIT,
}
compressed = blosc2.asarray(data, chunks=data.shape, blocks=data.shape, cparams=cparams)
decoded = compressed[...]
error = np.abs(decoded.astype(np.int32) - data.astype(np.int32))
payload = {
    "cbytes": int(compressed.schunk.cbytes),
    "nbytes": int(data.nbytes),
    "equal": bool(np.array_equal(decoded, data)),
    "max_abs": int(error.max()),
    "mean_abs": float(error.mean()),
}
assert decoded.dtype == data.dtype
assert decoded.shape == data.shape
assert not payload["equal"]
assert payload["cbytes"] < payload["nbytes"]
assert payload["max_abs"] <= 256
assert payload["mean_abs"] <= 40.0
print(json.dumps(payload))
"""
    proc = subprocess.run(
        [sys.executable, "-c", code],
        text=True,
        capture_output=True,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
    payload = json.loads(proc.stdout.strip().splitlines()[-1])
    assert payload["max_abs"] <= 256


def test_j2k_cli_list_plugins():
    proc = subprocess.run(
        [sys.executable, "-m", "blosc2_j2k", "--list-plugins"],
        text=True,
        capture_output=True,
        check=True,
    )
    payload = json.loads(proc.stdout)
    assert any(p["family"] == "j2k" for p in payload["plugins"])
