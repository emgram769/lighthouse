#!/bin/python

from multiprocessing import Value, Array, Process, Lock
from ctypes import c_char
import json
import sys
import time

MAX_QUERY_LEN =   1024
MAX_RESULT_LEN =  1024
MAX_RESULTS =     100
TIME_RESOLUTION = 1000

class Result(object):
  def __init__(self, title, action, descr = "", rank = 0):
    self.title = title
    self.action = action
    self.description = descr
    self.rank = rank

  def setRank(self, rank):
    self.rank = rank

  def toString(self):
    return json.dumps({
      "title" : self.title,
      "action" : self.action,
      "description" : self.description,
      "rank" : self.rank
    })

  @classmethod
  def fromString(cls, string):
    results = []
    results = json.loads(string)
    out = []
    for r in results:
      res = json.loads(r)
      out.append(cls(res["title"], res["action"], res["description"], int(res["rank"])))
    return out

  @classmethod
  def sanitizeOutput(cls, string, descr = False):
    string = string.replace("{", "\{")
    string = string.replace("}", "\}")
    string = string.replace("|", "\|")
    # Descriptions can handle new lines
    if not descr:
      string = string.replace("\n", " ")
    else:
      string = string.replayce("\n", "\\n")
    return string

  def toOutput(self):
    string = "{"
    string += Result.sanitizeOutput(self.title)
    string += "|"
    string += Result.sanitizeOutput(self.action)
    if self.description != "":
      string += "|"
      string += Result.sanitizeOutput(self.description, descr = True)
    string += "}"
    return string

class Module(object):
  def __init__(self):
    pass

  def getResultsWrapper(self, query, time, aggregator):
    results = self.getResults(query)
    if results:
      aggregator.addResult(query, time, results)

  def getResults(self, query):
    return []

class Aggregator(object):
  def __init__(self, modules = []):
    self.modules = modules
    self.currTime = int(time.time() * TIME_RESOLUTION)
    self.currQuery = Array(c_char, MAX_QUERY_LEN)
    self.currResults = Array(c_char, MAX_RESULT_LEN * MAX_RESULTS)
    self.lock = Lock()
    self.clearResults()

  def getCurrQuery(self):
    return self.currQuery.value.decode("utf-8")

  def setCurrQuery(self, query):
    self.currQuery.value = query.encode("utf-8")

  def getCurrResults(self):
    return Result.fromString(self.currResults.value.decode("utf-8"))

  def setCurrResults(self, results):
    resultsString = json.dumps([i.toString() for i in results])
    self.currResults.value = resultsString.encode("utf-8")
    print("".join([i.toOutput() for i in results]))
    sys.stdout.flush()

  def addModule(self, module):
    self.modules.append(module)

  def runQuery(self, query):
    self.setCurrQuery(query)
    self.currTime = int(time.time() * TIME_RESOLUTION)
    for module in self.modules:
      Process(target=module.getResultsWrapper, args=(query, self.currTime, self)).start()

  def clearResults(self):
    self.lock.acquire()
    self.setCurrResults([])
    self.lock.release()

  def addResult(self, query, time, results):
    self.lock.acquire()
    if query == self.getCurrQuery() and self.currTime == time:
      try:
        oldResults = self.getCurrResults()
        newResults = oldResults + results
        newResults = sorted(newResults, key=lambda x: x.rank)
        self.setCurrResults(newResults)
      except Exception as e:
        sys.stderr.write(str(e) + "\n")
    self.lock.release()

