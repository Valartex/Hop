"""Создаёт только тестовый WiX-проект с отказом после исполнения установки."""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
ns = 'http://wixtoolset.org/schemas/v4/wxs'
ET.register_namespace('', ns)
ET.register_namespace('ui', 'http://wixtoolset.org/schemas/v4/wxs/ui')
tree = ET.parse(args.source)
package = tree.getroot().find('{' + ns + '}Package')
if package is None or package.attrib.get('Name') != 'Hop':
    parser.error('Expected a Hop package')
ET.SubElement(package, '{' + ns + '}CustomAction',
    Id='HopRollbackTestFailure', Error='HOP_TEST_ROLLBACK_AFTER_EXECUTE')
sequence = package.find('{' + ns + '}InstallExecuteSequence')
ET.SubElement(sequence, '{' + ns + '}InstallExecute', After='StartServices')
ET.SubElement(sequence, '{' + ns + '}Custom', Action='HopRollbackTestFailure',
    After='InstallExecute', Condition='NOT Installed')
args.output.parent.mkdir(parents=True, exist_ok=True)
ET.indent(tree)
tree.write(args.output, encoding='utf-8', xml_declaration=True)
print('TEST ONLY: installation deliberately fails before InstallFinalize')
