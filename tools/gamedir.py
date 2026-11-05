# Finds the Just Cause 2 folder: the JC2_DIR environment variable if set, otherwise the Steam
# library that has it (Steam's libraryfolders.vdf).   python -I tools\gamedir.py  prints it.
import os, re


def find_game_dir():
    env = os.environ.get('JC2_DIR')
    if env:
        return env
    libraries = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as k:
            steam = winreg.QueryValueEx(k, 'SteamPath')[0]
        libraries.append(steam)
        vdf = open(os.path.join(steam, 'steamapps', 'libraryfolders.vdf'), encoding='utf-8').read()
        libraries += [p.replace('\\\\', '\\') for p in re.findall(r'"path"\s+"([^"]+)"', vdf)]
    except OSError:
        pass
    for lib in libraries:
        d = os.path.join(lib, 'steamapps', 'common', 'Just Cause 2')
        if os.path.isfile(os.path.join(d, 'JustCause2.exe')):
            return os.path.normpath(d)
    raise SystemExit('Just Cause 2 not found in Steam\'s libraries; set JC2_DIR to its folder')


if __name__ == '__main__':
    print(find_game_dir())
