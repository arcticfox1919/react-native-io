#!/usr/bin/env python3
"""Export an original library revision plus the current identical benchmark host."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    reports = Path(__file__).resolve().parents[1] / "reports"
    if args.output.resolve().is_relative_to(reports):
        parser.error("example/reports keeps only README.md; use a directory outside the repository")
    root = Path(__file__).resolve().parents[2]
    base = args.output.resolve()
    if base.exists():
        parser.error("output must not already exist; no existing directory is overwritten")
    commit = subprocess.check_output(["git", "rev-parse", "--verify", args.commit + "^{commit}"], cwd=root, text=True).strip()
    base.mkdir(parents=True)
    archive = base / "original-source.tar"
    with archive.open("wb") as out:
        subprocess.run(["git", "archive", "--format=tar", commit], cwd=root, stdout=out, check=True)
    subprocess.run(["tar", "-xf", str(archive), "-C", str(base)], check=True)
    archive.unlink()
    # Baseline implementations must be byte-for-byte original, not only similarly named.
    count = 0
    tree = subprocess.check_output(["git", "ls-tree", "-r", commit, "src", "cpp", "android"], cwd=root, text=True)
    for line in tree.splitlines():
        meta, name = line.split("\t")
        expected = meta.split()[2]
        data = (base / name).read_bytes()
        actual = hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()
        if actual != expected:
            raise RuntimeError(f"baseline mismatch: {name}")
        count += 1
    shutil.rmtree(base / "example")
    shutil.copytree(root / "example", base / "example", ignore=shutil.ignore_patterns(
        "node_modules", "build", ".cxx", ".gradle", "ios", "reports", "__pycache__"))
    (base / "node_modules").symlink_to(root / "node_modules", target_is_directory=True)
    (base / "example/node_modules").symlink_to(root / "example/node_modules", target_is_directory=True)
    # Original release integration lacks keep rules for JNI string-based lookups.
    # Supply them in the baseline host only; leave the library implementation untouched.
    with (base / "example/android/app/proguard-rules.pro").open("a") as out:
        out.write("\n# Benchmark host compatibility for the original JNI ABI.\n")
        out.write((root / "android/consumer-rules.pro").read_text())
    metro = base / "example/metro.config.js"
    with metro.open("a") as out:
        out.write("\nconst baselineFactory = module.exports;\n")
        out.write("module.exports = async (...args) => {\n")
        out.write("  const config = await (typeof baselineFactory === 'function' ? baselineFactory(...args) : baselineFactory);\n")
        out.write("  config.watchFolders = [...(config.watchFolders || []), " + json.dumps(str(root)) + "];\n")
        out.write("  const previousResolve = config.resolver.resolveRequest;\n")
        out.write("  config.resolver.resolveRequest = (context, moduleName, platform) => {\n")
        out.write("    if (moduleName === 'react-native-io') return {type: 'sourceFile', filePath: path.join(root, 'src/index.tsx')};\n")
        out.write("    return previousResolve ? previousResolve(context, moduleName, platform) : context.resolveRequest(context, moduleName, platform);\n")
        out.write("  };\n  return config;\n};\n")
    provenance = {"commit": commit, "verifiedOriginalLibraryFiles": count,
                  "baselineLibrary": "original src/, cpp/, android/; no algorithm changes",
                  "hostAdaptations": ["identical current example/benchmark harness", "original JNI keep rules supplied in host for R8",
                                      "explicit baseline JS resolver and symlinked installed dependencies"]}
    (base / "benchmark-baseline-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(base)


if __name__ == "__main__":
    main()
