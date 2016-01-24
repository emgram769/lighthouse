#!/bin/python

import lhapi
import sys
from modules import importModules

if __name__ == "__main__":
  aggro = lhapi.Aggregator(importModules())
  while True:
    userInput = sys.stdin.readline()
    userInput = userInput[:-1]
    aggro.clearResults()
    aggro.runQuery(userInput)

