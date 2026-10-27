"""Хеш состояния исходников без записи содержимого файлов и локальных секретов."""
from pathlib import Path
import hashlib
import json
import subprocess


def fingerprint(root):
    names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).split(b'\0')
    records = []
    for raw in sorted(set(names)):
        if not raw:
            continue
        name = raw.decode('utf-8')
        path = root / name
        # Gitlinks учитываются отдельно; внешние символьные ссылки не читаются.
        if path.is_symlink():
            records.append((name, 'symlink', str(path.readlink())))
        elif path.is_file():
            with path.open('rb') as stream:
                records.append((name, 'file', hashlib.file_digest(stream, 'sha256').hexdigest()))
        elif not path.exists():
            records.append((name, 'deleted', ''))
    entries = subprocess.check_output(['git', '-C', str(root), 'ls-files', '--stage', '-z']).split(b'\0')
    for entry in entries:
        if entry.startswith(b'160000 '):
            info, name = entry.split(b'\t', 1)
            records.append((name.decode(), 'gitlink', info.decode().split()[1]))
    encoded = json.dumps(sorted(records), ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    return {'algorithm': 'SHA-256', 'digest': hashlib.sha256(encoded).hexdigest(), 'entries': len(records),
            'scope': 'nonignored Git working tree files and index gitlinks; toolchain/dependency caches excluded'}


if __name__ == '__main__':
    print(json.dumps(fingerprint(Path(__file__).resolve().parents[2]), separators=(',', ':')))
