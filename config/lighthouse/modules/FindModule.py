import lhapi
from time import sleep
import subprocess
import os
import re

class FindModule(lhapi.Module):
  def isText(self, fileName):
    msg = subprocess.check_output(["file", fileName])
    return re.search(b'text', msg) != None
  def getResults(self, query):
    # Don't be too aggressive...
    sleep(.5)

    try:
      find_out = subprocess.check_output(
                    ["find",
                     os.path.expanduser("~"),
                     "-name",
                     query]).decode("utf-8")
    except subprocess.CalledProcessError as e:
      find_out = str(e.output)

    find_array = find_out.split("\n")[:-1]
    if (len(find_array) == 0): return
    results = []

    for i in range(min(5, len(find_array))):
      title = str(find_array[i])
      action = "urxvt -e "
      if self.isText(title):
        action += "vim " + title
      else:
        action += "bash -c 'cd $(dirname " + title + "); ls; bash;'"

      # Run in background
      action += " &";
      r = lhapi.Result(title, action, "", 99)
      results.append(r)
    return results
