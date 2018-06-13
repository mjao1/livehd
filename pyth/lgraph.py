from __future__ import unicode_literals

import sys
import os

from ptpython.repl import embed
from pyth import lgraph

base_dir = os.path.dirname(sys.argv[0]) or '.'
print('Base directory:', base_dir)
#print(os.listdir(base_dir))

# Insert the package_dir_a directory at the front of the path.
package_dir_a = os.path.join(base_dir, '__main__')
sys.path.insert(0, package_dir_a)

def main():
    embed(globals(), locals(), vi_mode=False)

if __name__ == '__main__':
    main()

