import json
import subprocess
import sys

import blosc2
import blosc2_j2k
import numpy as np
import pytest


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


def test_j2k_uint32_lossless_roundtrip_with_kakadu_if_available():
    code = r"""
import json

import blosc2_j2k

if "kakadu" not in blosc2_j2k.available_backends()["j2k"]:
    print(json.dumps({"skipped": True}))
    raise SystemExit(0)

import blosc2
import numpy as np

blosc2_j2k.register_codec()
blosc2_j2k.configure(backend="kakadu")

base = np.arange(48 * 64, dtype=np.uint32).reshape(48, 64)
data = ((base * np.uint32(104729)) ^ (base << np.uint32(16)) ^ np.uint32(0x80000000)).astype(np.uint32)
data[0, 0] = np.uint32(0)
data[0, 1] = np.iinfo(np.uint32).max
data[0, 2] = np.uint32(2**31)
data[0, 3] = np.uint32(2**31 - 1)

cparams = {
    "codec": blosc2_j2k.CODEC_ID,
    "filters": [],
    "splitmode": blosc2.SplitMode.NEVER_SPLIT,
}
compressed = blosc2.asarray(data, chunks=data.shape, blocks=data.shape, cparams=cparams)
decoded = compressed[...]
np.testing.assert_array_equal(decoded, data)
print(json.dumps({
    "skipped": False,
    "dtype": str(decoded.dtype),
    "min": int(decoded.min()),
    "max": int(decoded.max()),
}))
"""
    proc = subprocess.run(
        [sys.executable, "-c", code],
        text=True,
        capture_output=True,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
    payload = json.loads(proc.stdout.strip().splitlines()[-1])
    if payload["skipped"]:
        pytest.skip("Kakadu J2K backend is not installed")
    assert payload["dtype"] == "uint32"
    assert payload["max"] == 2**32 - 1


def test_j2k_cli_list_plugins():
    proc = subprocess.run(
        [sys.executable, "-m", "blosc2_j2k", "--list-plugins"],
        text=True,
        capture_output=True,
        check=True,
    )
    payload = json.loads(proc.stdout)
    assert any(p["family"] == "j2k" for p in payload["plugins"])
