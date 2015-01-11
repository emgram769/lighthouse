#!/usr/bin/python2.7

import sys
import random
from time import sleep
from google import pygoogle
from multiprocessing import Process, Value, Manager, Array
from ctypes import c_char
import subprocess

MAX_OUTPUT = 1024

resultStr = Array(c_char, MAX_OUTPUT);

def clear_output():
  resultStr.value = ""

def append_output(title, action):
  title = title.replace("{", "<").replace("}", ">").replace("|", ":")
  action = action.replace("{", "<").replace("}", ">").replace("|", ":")
  resultStr.value += "{"+title+"|"+action+"}"

def update_output():
  print resultStr.value
  sys.stdout.flush()
  
google_thr = None
def google(query):
  sleep(.5) # so we aren't querying EVERYTHING we type
  g = pygoogle(userInput)
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
  "chrom": (lambda x: ("did you mean firefox?","firefox"))
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

  # Scan for keywords
  for keyword in special:
    if userInput[0:len(keyword)] == keyword:
      out = special[keyword](userInput)
      if out != None:
        append_output(*out);

  # Is this python?
  try:
    out = eval(userInput)
    if (type(out) != str and str(out)[0] == '<'):
      pass # We don't want gibberish type stuff
    else:
      append_output("python: "+str(out), "urxvt -e python2.7 -i -c 'print "+userInput+"'")
  except Exception as e:
    pass

  # Could be a command...
  append_output("execute '"+userInput+"'", userInput);

  # Could be bash...
  append_output("run '"+userInput+"' in a shell", "urxvt -e bash -c '"+userInput+" && bash'");
 
  update_output()
  
