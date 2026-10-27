"""Детерминированный список файлов MSI из проверенного stage, без тестового агента."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import uuid
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--stage', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', default=(Path(__file__).with_name('windows-version.txt')).read_text(encoding='ascii').strip())
args = parser.parse_args()
if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', args.version):
    parser.error('MSI version must contain exactly three numeric fields')
if any(value > limit for value, limit in zip(map(int, args.version.split('.')), (255, 255, 65535))):
    parser.error('MSI version exceeds Windows Installer limits')
stage = args.stage.resolve(strict=True)
actions = Path(__file__).resolve().parents[2] / 'build/installer/HopInstallerActions.dll'
if not actions.is_file():
    parser.error('Build HopInstallerActions.dll with build-installer.ps1 first')
ns = 'http://wixtoolset.org/schemas/v4/wxs'
ui_ns = 'http://wixtoolset.org/schemas/v4/wxs/ui'
ET.register_namespace('', ns)
ET.register_namespace('ui', ui_ns)

def element(parent, name, **attributes):
    return ET.SubElement(parent, '{' + ns + '}' + name, {k: str(v) for k, v in attributes.items()})

def identity(prefix, text):
    return prefix + hashlib.sha256(text.lower().encode()).hexdigest()[:26]

root = ET.Element('{' + ns + '}Wix')
product_code = str(uuid.uuid5(uuid.UUID('71037df9-cb71-452b-9d8a-80075b243feb'), args.version))
package = element(root, 'Package', Name='Hop', Manufacturer='Hop', Version=args.version, ProductCode=product_code, Language='1049',
    UpgradeCode='FAE4DDF6-DA49-4DAE-9A6C-1D591CB5F57F', Scope='perMachine', InstallerVersion='500')
# Явные Upgrade-строки позволяют поставить rollback до RemoveExistingProducts.
upgrade = element(package, 'Upgrade', Id='FAE4DDF6-DA49-4DAE-9A6C-1D591CB5F57F')
element(upgrade, 'UpgradeVersion', Minimum='0.0.0', IncludeMinimum='yes', Maximum=args.version,
    IncludeMaximum='no', Property='WIX_UPGRADE_DETECTED', MigrateFeatures='yes', OnlyDetect='no')
element(upgrade, 'UpgradeVersion', Minimum=args.version, IncludeMinimum='no',
    Property='WIX_DOWNGRADE_DETECTED', MigrateFeatures='no', OnlyDetect='yes')
element(package, 'Launch', Condition='Installed OR NOT WIX_DOWNGRADE_DETECTED', Message='Установлена более новая версия Hop.')
element(package, 'MediaTemplate', EmbedCab='yes', CompressionLevel='high')
build = element(package, 'Property', Id='HOP_OS_BUILD', Secure='yes')
element(build, 'RegistrySearch', Id='HopActualWindowsBuild', Root='HKLM',
    Key=r'SOFTWARE\Microsoft\Windows NT\CurrentVersion', Name='CurrentBuildNumber', Type='raw', Bitness='always64')
element(package, 'Launch', Condition='Installed OR (VersionNT64 AND HOP_OS_BUILD >= 17763)',
    Message='Для Hop требуется Windows 10 x64 версии 1809 или новее.')
element(package, 'Property', Id='ARPPRODUCTICON', Value='HopIcon')
location = element(package, 'Property', Id='INSTALLFOLDER', Secure='yes')
element(location, 'RegistrySearch', Id='HopPreviousInstallFolder', Root='HKLM',
    Key=r'Software\Hop\Installer', Name='InstallLocation', Type='raw', Bitness='always64')
element(package, 'Icon', Id='HopIcon', SourceFile=str(stage / 'Hop.exe'))
program_files = element(package, 'StandardDirectory', Id='ProgramFiles64Folder')
install = element(program_files, 'Directory', Id='INSTALLFOLDER', Name='Hop')
directories = {Path('.'): install}
feature = element(package, 'Feature', Id='HopClient', Title='Hop', Level='1')
component_namespace = uuid.UUID('d25b3ce3-3bf1-47ca-acdf-df3fae4cab27')
excluded = {'hop_windows_vpn_probe.exe', 'hop_xray_probe.exe', 'hop_gateway_probe.exe',
            'hop_foreign_ipc_probe.exe', 'DEVELOPMENT.txt', 'Hop-VM-Extra.json', 'Hop-Setup.msi'}
manifest = json.loads((stage / 'BUILD-MANIFEST.json').read_text(encoding='utf-8-sig'))
manifest['installer'] = {'version': args.version, 'productCode': product_code,
    'upgradeCode': 'FAE4DDF6-DA49-4DAE-9A6C-1D591CB5F57F'}
manifest['hashes'] = {name: value for name, value in manifest['hashes'].items() if name not in excluded}
args.output.parent.mkdir(parents=True, exist_ok=True)
payload_manifest = args.output.parent / 'PAYLOAD-MANIFEST.json'
payload_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
count = 0
for file in sorted(stage.rglob('*'), key=lambda p: str(p.relative_to(stage)).lower()):
    if not file.is_file():
        continue
    relative = file.relative_to(stage)
    if file.name in excluded or file.name.startswith('Hop-VM-') or file.suffix.lower() in {'.pdb', '.ps1', '.cmd', '.msi'}:
        continue
    if any(part.lower() == 'qmltooling' for part in relative.parts):
        continue
    parent = Path('.')
    for part in relative.parts[:-1]:
        child = parent / part
        if child not in directories:
            directories[child] = element(directories[parent], 'Directory', Id=identity('D', child.as_posix()), Name=part)
        parent = child
    component_id = identity('C', relative.as_posix())
    component = element(directories[parent], 'Component', Id=component_id,
        Guid=str(uuid.uuid5(component_namespace, relative.as_posix().lower())))
    file_id = 'HopClientFile' if relative.as_posix() == 'Hop.exe' else 'HopRecoveryFile' if relative.as_posix() == 'hop_guard_recover.exe' else identity('F', relative.as_posix())
    element(component, 'File', Id=file_id, Source=str(payload_manifest if relative.as_posix() == 'BUILD-MANIFEST.json' else file), Name=file.name, KeyPath='yes')
    if relative.as_posix() == 'Hop-service.exe':
        service = element(component, 'ServiceInstall', Id='HopServiceInstall', Name='Hop-service', DisplayName='Hop Service',
            Description='VPN-служба Hop', Type='ownProcess', Start='auto', ErrorControl='normal', Vital='yes')
        element(service, 'ServiceDependency', Id='BFE')
        element(service, 'ServiceDependency', Id='nsi')
        element(component, 'ServiceControl', Id='HopServiceControl', Name='Hop-service', Start='install', Stop='both', Remove='uninstall', Wait='yes')
    element(feature, 'ComponentRef', Id=component_id)
    count += 1

menu = element(package, 'StandardDirectory', Id='ProgramMenuFolder')
menu_folder = element(menu, 'Directory', Id='HopMenuFolder', Name='Hop')
shortcuts = element(menu_folder, 'Component', Id='HopShortcuts', Guid='CECC67D0-6871-4B1C-BF7B-A51DE63E7A36')
element(shortcuts, 'Shortcut', Id='HopStartMenuShortcut', Name='Hop', Target='[#HopClientFile]', WorkingDirectory='INSTALLFOLDER', Icon='HopIcon')
element(shortcuts, 'RemoveFolder', Id='HopRemoveMenuFolder', On='uninstall')
element(shortcuts, 'RegistryValue', Root='HKLM', Key=r'Software\Hop\Installer', Name='Installed', Type='integer', Value='1', KeyPath='yes')
element(shortcuts, 'RegistryValue', Root='HKLM', Key=r'Software\Hop\Installer', Name='InstallLocation', Type='string', Value='[INSTALLFOLDER]')
element(feature, 'ComponentRef', Id='HopShortcuts')

# Upgrade сохраняет защиту; явное удаление снимает только собственные правила Hop.
element(package, 'CustomAction', Id='HopStopOwnedTunnel', FileRef='HopRecoveryFile', ExeCommand='--stop-owned-hop-tunnel',
    Execute='deferred', Impersonate='no', Return='check')
element(package, 'CustomAction', Id='HopRemoveProtection', FileRef='HopRecoveryFile', ExeCommand='--uninstall-hop',
    Execute='deferred', Impersonate='no', Return='check')
sequence = element(package, 'InstallExecuteSequence')

# Rollback выполняется в обратном порядке: регистрируем восстановление до удаления старой версии.
element(package, 'Binary', Id='HopInstallerActions', SourceFile=str(actions))
element(package, 'CustomAction', Id='HopPrepareServiceRollback', BinaryRef='HopInstallerActions',
    DllEntry='PrepareHopServiceRollback', Execute='immediate', Return='check')
element(package, 'CustomAction', Id='HopRestoreServiceOnRollback', BinaryRef='HopInstallerActions',
    DllEntry='RestoreHopService', Execute='rollback', Impersonate='no', Return='check')
upgrade_condition = 'WIX_UPGRADE_DETECTED AND NOT Installed'
element(sequence, 'Custom', Action='HopPrepareServiceRollback', After='InstallInitialize', Condition=upgrade_condition)
element(sequence, 'Custom', Action='HopRestoreServiceOnRollback', After='HopPrepareServiceRollback', Condition=upgrade_condition)
# Явно задаём порядок внутри транзакции до RemoveExistingProducts.
element(sequence, 'RemoveExistingProducts', After='HopRestoreServiceOnRollback')
element(sequence, 'Custom', Action='HopStopOwnedTunnel', After='StopServices', Condition='Installed')
element(sequence, 'Custom', Action='HopRemoveProtection', After='HopStopOwnedTunnel', Condition='REMOVE~="ALL" AND NOT UPGRADINGPRODUCTCODE')
ET.SubElement(package, '{' + ui_ns + '}WixUI', {'Id': 'WixUI_InstallDir', 'InstallDirectory': 'INSTALLFOLDER'})
element(package, 'WixVariable', Id='WixUILicenseRtf', Value=str(args.output.parent / 'license.rtf'))
license_text = (stage / 'LICENSE').read_text(encoding='utf-8')
escaped = ''.join('\\' + ch if ch in '\\{}' else '\\u' + str(ord(ch) if ord(ch) < 32768 else ord(ch) - 65536) + '?' if ord(ch) > 127 else ch for ch in license_text)
args.output.parent.mkdir(parents=True, exist_ok=True)
(args.output.parent / 'license.rtf').write_text('{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\f0\\fs18 ' + escaped.replace('\n', '\\par\n') + '}', encoding='ascii')
ET.indent(root)
ET.ElementTree(root).write(args.output, encoding='utf-8', xml_declaration=True)
print(f'MSI source: {count} files, no VM agent or test executables')
