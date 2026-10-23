#!/usr/bin/env python3
"""Remove this user's native-package login entry; retain AppImage and custom entries."""
import argparse
import configparser
import os
from pathlib import Path
import re


def unescape(value):
    escapes = {'s': ' ', 'n': '\n', 't': '\t', 'r': '\r', '\\': '\\'}
    return re.sub(r'\\([sntr\\])', lambda match: escapes[match[1]], value)


def cleanup(config_root, executable):
    entry = config_root / 'autostart/fxsound.desktop'
    if not entry.is_file():
        return False
    parser = configparser.ConfigParser(interpolation=None)
    try:
        parser.read_string(entry.read_text(encoding='utf-8'))
        values = parser['Desktop Entry']
        if values.get('Type') != 'Application' or values.get('Name') != 'FxSound':
            return False
        kind = values.get('X-FxSound-LaunchKind')
        if kind == 'native':
            owned = unescape(values.get('TryExec', '')) == executable
        elif kind is None:
            # Entries generated before launch-kind metadata used exactly one
            # unadorned executable token. Preserve custom commands/arguments.
            owned = values.get('Exec', '').strip() in (executable, f'"{executable}"', 'fxsound', '"fxsound"')
        else:
            owned = False
        if owned:
            entry.unlink()
        return owned
    except (configparser.Error, KeyError, UnicodeError):
        return False


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', default='/usr/bin/fxsound')
    parser.add_argument('--config-root', type=Path)
    args = parser.parse_args()
    root = args.config_root
    if root is None:
        configured = Path(os.environ.get('XDG_CONFIG_HOME', ''))
        root = configured if configured.is_absolute() else Path.home()/'.config'
    if cleanup(root, args.executable):
        print('Removed native FxSound launch-on-login entry')
