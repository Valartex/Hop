"""Инвентаризация фактического MSI payload; отсутствие лицензии не заменяется догадкой."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import uuid
import xml.etree.ElementTree as ET


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--wxs', type=Path, required=True)
    parser.add_argument('--graph', type=Path, required=True)
    parser.add_argument('--go', type=Path, required=True)
    parser.add_argument('--module-cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    ns = {'w': 'http://wixtoolset.org/schemas/v4/wxs'}
    xml = ET.parse(args.wxs)
    files = []

    def collect(directory, parent=''):
        relative = parent + directory.attrib.get('Name', '') + '/'
        for element in directory:
            if element.tag.endswith('}Directory'):
                collect(element, relative)
            elif element.tag.endswith('}Component'):
                for file in element.findall('w:File', ns):
                    source = Path(file.attrib['Source'])
                    files.append((relative + file.attrib['Name'], source))

    collect(xml.find('.//w:Directory[@Id="INSTALLFOLDER"]', ns))
    for binary in xml.findall('w:Package/w:Binary', ns):
        source = Path(binary.attrib['SourceFile'])
        files.append(('@installer/' + source.name, source))
    components = []
    evidence = []
    refs = set()

    def component(kind, name, version='', properties=None):
        ref = kind + ':' + name + ('@' + version if version else '')
        if ref in refs:
            return None
        refs.add(ref)
        entry = {'type': kind, 'bom-ref': ref, 'name': name}
        if version:
            entry['version'] = version
        if properties:
            entry['properties'] = [{'name': 'hop:' + key, 'value': str(value)} for key, value in sorted(properties.items())]
        components.append(entry)
        return entry

    for name, source in sorted(files):
        item = component('file', name)
        item['hashes'] = [{'alg': 'SHA-256', 'content': digest(source)}]
    # Граф включает статические и транзитивные зависимости сборки, а не только DLL.
    graph = json.loads(args.graph.read_text(encoding='utf-8-sig'))['graph']['nodes']
    for node in graph.values():
        ref = node.get('ref')
        if node.get('context') != 'host' or not ref:
            continue
        name, version = ref.split('#')[0].split('/', 1)
        item = component('library', name, version, {'conan-reference': ref, 'evidence': 'build-resolution-graph'})
        if item is None:
            continue
        if node.get('license'):
            # Сохраняем исходные сведения Conan без уточнения неизвестных SPDX-вариантов.
            item['licenses'] = [{'license': {'name': str(node['license'])}}]
        else:
            evidence.append({'component': item['bom-ref'], 'status': 'license-metadata-missing'})

    modules = {}
    for name, source in files:
        if source.name not in {'agw.dll', 'amnezia_xray.dll', 'tunnel.dll', 'tun2socks.exe', 'hop_awg_probe.exe'}:
            continue
        result = subprocess.run([str(args.go), 'version', '-m', str(source)], capture_output=True, text=True, timeout=20, check=True)
        pending = None
        for line in result.stdout.splitlines():
            fields = line.strip().split()
            if fields and fields[0] == 'dep' and len(fields) >= 3:
                pending = (fields[1], fields[2])
                modules.setdefault(pending, set()).add(name)
            elif fields and fields[0] == '=>' and len(fields) >= 3 and pending:
                modules[pending].discard(name)
                if not modules[pending]:
                    del modules[pending]
                modules.setdefault((fields[1], fields[2]), set()).add(name)
                pending = None
    for (name, version), binaries in sorted(modules.items()):
        item = component('library', name, version, {'evidence': 'embedded-go-build-info', 'binaries': ', '.join(sorted(binaries))})
        escaped = re.sub('[A-Z]', lambda match: '!' + match[0].lower(), name)
        folder = args.module_cache / (escaped + '@' + version)
        texts = [file for file in folder.glob('*') if file.is_file() and re.match(r'^(LICENSE|COPYING|NOTICE)(\.|$)', file.name, re.I)]
        for text in texts:
            target = args.output / 'license-evidence' / hashlib.sha256((name + '@' + version).encode()).hexdigest()[:20] / text.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(text, target)
        evidence.append({'component': item['bom-ref'], 'status': 'license-text-found-review-required' if texts else 'license-text-missing',
                         'texts': [str((Path('license-evidence') / hashlib.sha256((name + '@' + version).encode()).hexdigest()[:20] / text.name).as_posix()) for text in texts]})
    component('framework', 'Qt', '6.10.1', {'evidence': 'pinned-build-toolchain', 'license-review': 'pending including bundled third parties'})
    identity = hashlib.sha256(json.dumps(components, sort_keys=True).encode()).hexdigest()
    sbom = {'bomFormat': 'CycloneDX', 'specVersion': '1.6', 'serialNumber': 'urn:uuid:' + str(uuid.uuid5(uuid.NAMESPACE_URL, 'hop:' + identity)),
            'version': 1, 'metadata': {'component': {'type': 'application', 'name': 'Hop', 'version': xml.find('w:Package', ns).attrib['Version'], 'bom-ref': 'Hop'}},
            'components': components, 'dependencies': [{'ref': 'Hop', 'dependsOn': sorted(refs)}]}
    (args.output / 'hop-windows.cdx.json').write_text(json.dumps(sbom, ensure_ascii=False, indent=2), encoding='utf-8')
    (args.output / 'license-review.json').write_text(json.dumps({'releaseCleared': False, 'notes': [
        'Inventory is not a license clearance or complete source offer.',
        'libagw 1.0.6 source archive has no LICENSE/COPYING; resolve before public redistribution.',
        'Qt bundled libraries, Microsoft runtime, driver and static C++ notices still require review.'
    ], 'components': evidence}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'SBOM: {len(files)} payload files, {len(components)} components; release license review remains open')


if __name__ == '__main__':
    main()
