from os.path import dirname, basename, isfile
from os import chdir, getcwd
import glob

import sys

def importModules():
  modules = glob.glob(dirname(__file__) + "/*.py")
  files = [ basename(f)[:-3] for f in modules if isfile(f) and not basename(f)[0] == '_' ]
  return [getattr(getattr(__import__('modules.'+f), f), f)() for f in files]
