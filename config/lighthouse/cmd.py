#!/usr/bin/python2.7

import sys
import random
from time import sleep
import logging
from google import pygoogle
from multiprocessing import Process, Value, Manager, Array
from ctypes import c_char
import subprocess

MAX_OUTPUT = 100 * 1024

resultStr = Array(c_char, MAX_OUTPUT);

def clear_output():
  resultStr.value = ""

def append_output(title, action):
  title = title.replace("{", "<").replace("}", ">").replace("|", ":")
  action = action.replace("{", "<").replace("}", ">").replace("|", ":")
  if resultStr.value == "":
    resultStr.value = "{"+title+"|"+action+"}"
  else: # ignore the bottom two default options
    arr = resultStr.value.split("{")
    insert = -2
    if (len(arr) <= 2):
      insert = -1
    arr.insert(insert, title+"|"+action+"}")
    resultStr.value = "{".join(arr)

def prepend_output(title, action):
  title = title.replace("{", "<").replace("}", ">").replace("|", ":")
  action = action.replace("{", "<").replace("}", ">").replace("|", ":")
  resultStr.value = "{"+title+"|"+action+"}" + resultStr.value

def update_output():
  print resultStr.value
  sys.stdout.flush()
  
google_thr = None
def google(query):
  sleep(.5) # so we aren't querying EVERYTHING we type
  g = pygoogle(userInput, log_level=logging.CRITICAL)
  g.pages = 1
  out = g.get_urls()
  if (len(out) >= 1):  
    append_output(out[0], "firefox " + out[0])
    update_output()
  
find_thr = None
def find(query):
  find_out = str(subprocess.check_output(["find", "/home", "-name", query]))
  find_array = find_out.split("\n")[:-1]
  if (len(find_array) == 0): return
  for i in xrange(min(5, len(find_array))):
    append_output(str(find_array[i]),"urxvt -e bash -c 'cd $(dirname "+find_array[i]+"); bash'");
  update_output()

special = {
  "chrom": (lambda x: ("did you mean firefox?","firefox")),
  "fire": (lambda x: ("firefox","firefox")),
  "vi": (lambda x: ("vim","urxvt -e vim"))
};

while 1:
  userInput = sys.stdin.readline()
  userInput = userInput[:-1]

  # Clear results
  clear_output()

  # Kill previous worker threads
  if google_thr != None:
    google_thr.terminate()
  if find_thr != None:
    find_thr.terminate()

  # We don't handle empty strings
  if userInput == '':
    update_output()
    continue

  # Spawn worker threads
  google_thr = Process(target=google, args=(userInput,))
  google_thr.start()
  find_thr = Process(target=find, args=(userInput,))
  find_thr.start()

  # Could be a command...
  append_output("execute '"+userInput+"'", userInput);

  # Could be bash...
  append_output("run '"+userInput+"' in a shell", "urxvt -e bash -c '"+userInput+" && bash'");

  # Scan for keywords
  for keyword in special:
    if userInput[0:len(keyword)] == keyword:
      out = special[keyword](userInput)
      if out != None:
        prepend_output(*out);

  # Is this python?
  try:
    out = eval(userInput)
    if (type(out) != str and str(out)[0] == '<'):
      pass # We don't want gibberish type stuff
    else:
      prepend_output("python: "+str(out), "urxvt -e python2.7 -i -c 'print "+userInput+"'")
  except Exception as e:
    pass

 
  update_output()
  
