#!/usr/bin/env python3
from pathlib import Path

async def compile_(dbg=True):
    for dir_ in Path('.').iterdir():
        files = set(dir_.iterdir())
        if dir_ == 'main':
            subprocess.run(['make', '-C', dir_, 'main.dbg.exe'])
        elif 'Makefile' in files:
            subprocess.run(['make', '-C', dir_, 'plugin.dbg.so'])


if __name__ == '__main__':
    # cd to the dir containing this script
    import os
    os.chdir(Path(__file__).absolute().parent)

    compile_()
    run()
