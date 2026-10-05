"""Verify the real CMake extraction rule against read-only synthetic originals."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


def run(command, expected_success=True):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if (result.returncode == 0) != expected_success:
        raise RuntimeError(result.stdout)
    return result.stdout


def archive(name, data):
    names = b"\0" + name.encode() + b"\0"
    offset = 32 + 32 + len(names)
    return (struct.pack(">4I", 0x55AA382D, 32, 32 + len(names), offset) + bytes(16)
            + struct.pack(">4I", 0x01000000, 0, 2, 0)
            + struct.pack(">4I", 1, offset, len(data), 0) + names + data)


def snapshot(directory):
    return {str(p.relative_to(directory)): (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns)
            for p in directory.rglob("*") if p.is_file()}


def cmake_quote(path):
    return str(path).replace("\\", "/").replace('"', '\\"')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--u8extract", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    extractor = args.u8extract.resolve()
    if not extractor.is_file():
        parser.error("Build u8extract first, then pass its native executable path.")

    with tempfile.TemporaryDirectory(prefix="marathon-input-check-") as temporary:
        work = Path(temporary)
        harness = work / "harness"
        harness.mkdir()
        (harness / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.20)
project(GameInputChecks LANGUAGES C CXX)
set(MARATHON_RECOMP_HOST_TOOLS_ONLY ON)
set(MARATHON_RECOMP_IOS ON)
set(MARATHON_RECOMP_TOOLS_ROOT "{cmake_quote(repo / 'tools')}")
include("{cmake_quote(repo / 'cmake/GameInputs.cmake')}")
foreach(tool u8extract XenonRecomp XenosRecomp)
    add_executable(${{tool}} IMPORTED GLOBAL)
    set_target_properties(${{tool}} PROPERTIES IMPORTED_LOCATION "{cmake_quote(extractor)}")
endforeach()
add_subdirectory("{cmake_quote(repo / 'MarathonRecompLib')}" lib)
''')

        for layout in ("flat", "extracted"):
            original = work / layout / "Sonic the Hedgehog (USA, Europe) (En,Ja,Fr,De,Es,It)"
            original.mkdir(parents=True)
            (original / "default.xex").write_bytes(b"synthetic input; never recompiled")
            archives = original if layout == "flat" else original / "xenon/archives"
            archives.mkdir(parents=True, exist_ok=True)
            (archives / "shader.arc").write_bytes(archive("main.shader", b"main test bytes"))
            (archives / "shader_lt.arc").write_bytes(archive("lt.shader", b"lt test bytes"))
            before = snapshot(original)
            directories = [original] + [p for p in original.rglob("*") if p.is_dir()]
            files = [p for p in original.rglob("*") if p.is_file()]
            for p in files:
                p.chmod(0o444)
            for p in directories:
                p.chmod(0o555)
            try:
                run([args.cmake, f"-DMARATHON_RECOMP_GAME_INPUT_DIR={original}", "-P", str(repo / "cmake/CheckGameInputs.cmake")])
                build = work / layout / "build"
                generated = work / layout / "generated"
                run([args.cmake, "-S", str(harness), "-B", str(build),
                     f"-DMARATHON_RECOMP_GAME_INPUT_DIR={original}", f"-DMARATHON_RECOMP_GENERATED_DIR={generated}"])
                run([args.cmake, "--build", str(build), "--target", "marathon_extract_shaders"])
                assert (generated / "shader/input/main.shader").read_bytes() == b"main test bytes"
                assert (generated / "shader/input/lt.shader").read_bytes() == b"lt test bytes"
                assert (generated / "shader/extracted.stamp").is_file()
                config = (build / "lib/Marathon.toml").read_text()
                # XenonRecomp resolves config paths against the config's own directory.
                xex_path = next(line.split('"')[1] for line in config.splitlines() if line.startswith("file_path = "))
                assert not Path(xex_path).is_absolute()
                assert (build / "lib" / xex_path).resolve() == (original / "default.xex").resolve()
                error = run([args.cmake, "-S", str(harness), "-B", str(work / layout / "bad-build"),
                             f"-DMARATHON_RECOMP_GAME_INPUT_DIR={original}",
                             f"-DMARATHON_RECOMP_GENERATED_DIR={original / 'generated'}"], expected_success=False)
                assert "must be outside" in error
                assert not (original / "generated").exists()
                assert snapshot(original) == before
            finally:
                for p in directories:
                    p.chmod(0o755)
                for p in files:
                    p.chmod(0o644)

        missing = work / "missing"
        missing.mkdir()
        error = run([args.cmake, f"-DMARATHON_RECOMP_GAME_INPUT_DIR={missing}", "-P",
                     str(repo / "cmake/CheckGameInputs.cmake")], expected_success=False)
        assert "Missing original game input" in error
        (missing / "default.xex").write_bytes(b"")
        error = run([args.cmake, f"-DMARATHON_RECOMP_GAME_INPUT_DIR={missing}", "-P",
                     str(repo / "cmake/CheckGameInputs.cmake")], expected_success=False)
        assert "Original game input is empty" in error

    print("Passed: read-only flat/extracted inputs, spaced paths, real shader extraction, original hashes/mtime preserved, missing inputs, output overlap rejection.")


if __name__ == "__main__":
    main()
