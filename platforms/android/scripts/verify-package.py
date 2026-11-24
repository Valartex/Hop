"""Проверка ABI и ELF-выравнивания APK/AAB без распаковки на диск."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def verify(path: Path) -> dict:
    libraries = []
    with zipfile.ZipFile(path) as archive:
        invalid = archive.testzip()
        if invalid:
            raise ValueError("Повреждён элемент архива: " + invalid)
        for name in archive.namelist():
            if not name.endswith(".so") or "/lib/" not in "/" + name:
                continue
            if "/arm64-v8a/" not in name:
                raise ValueError("Неожиданный ABI: " + name)
            data = archive.read(name)
            if data[:6] != b"\x7fELF\x02\x01":
                raise ValueError("Ожидался ELF64 little-endian: " + name)
            offset = struct.unpack_from("<Q", data, 32)[0]
            size, count = struct.unpack_from("<HH", data, 54)
            alignments = [
                struct.unpack_from("<Q", data, offset + i * size + 48)[0]
                for i in range(count)
                if struct.unpack_from("<I", data, offset + i * size)[0] == 1
            ]
            if not alignments or any(value < 16384 for value in alignments):
                raise ValueError("ELF несовместим со страницами 16 КБ: " + name)
            libraries.append(name)
    if not libraries:
        raise ValueError("В пакете отсутствуют нативные библиотеки")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"file": path.name, "bytes": path.stat().st_size, "sha256": digest,
            "abi": "arm64-v8a", "nativeLibraries": len(libraries), "elfPageSize": 16384}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    result = verify(args.package)
    args.package.with_suffix(args.package.suffix + ".json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
