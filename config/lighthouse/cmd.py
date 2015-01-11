#!/usr/bin/python2.7

import sys
import random
from time import sleep

while 1:
  userInput = sys.stdin.readline()
  userInput = userInput[:-1]

  # We don't handle empty strings
  if userInput == '':
    print ""
    sys.stdout.flush()
    continue

  # Populate a list of results
  results = [];

  # Is this python?
  try:
    out = eval(userInput)
    results.append("{python: "+str(out)+"|urxvt -e python2.7 -i -c 'print "+userInput+"'}")
  except Exception as e:
    pass

  # Could be a command...
  results.append("{execute '"+userInput+"'|"+userInput+"}");

  # Could be bash...
  results.append("{run '"+userInput+"' in a shell| urxvt -e bash -c '"+userInput+" && bash' }");
 
  print "".join(results)
  sys.stdout.flush()

