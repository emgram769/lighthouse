#!/usr/bin/python2.7

import sys
import random
from time import sleep
import logging
from google import pygoogle
from multiprocessing import Process, Value, Manager, Array
from ctypes import c_char, c_char_p
import subprocess
import json
import os

MAX_OUTPUT = 100 * 1024

resultStr = Array(c_char, MAX_OUTPUT);

def clear_output():
  resultStr.value = json.dumps([])

def sanitize_output(string):
  string = string.replace("{", "\{")
  string = string.replace("}", "\}")
  string = string.replace("|", "\|")
  string = string.replace("\n", " ")
  return string

def create_result(title, action):
  return "{" + title + " |" + action + " }"

def append_output(title, action):
  title = sanitize_output(title)
  action = sanitize_output(action)
  results = json.loads(resultStr.value)
  if len(results) < 2:
    results.append(create_result(title, action))
  else: # ignore the bottom two default options
    results.insert(-2, create_result(title, action))
  resultStr.value = json.dumps(results)

def prepend_output(title, action):
  title = sanitize_output(title)
  action = sanitize_output(action)
  results = json.loads(resultStr.value)
  results = [create_result(title, action)] + results
  resultStr.value = json.dumps(results)

def update_output():
  results = json.loads(resultStr.value)
  print "".join(results)
  sys.stdout.flush()
  
google_thr = None
def google(query):
  sleep(.5) # so we aren't querying EVERYTHING we type
  g = pygoogle(userInput, log_level=logging.CRITICAL)
  g.pages = 1
  out = g.get_urls()
  if (len(out) >= 1):  
    append_output(out[0], "xdg-open " + out[0])
    update_output()
  
find_thr = None
def find(query):
  sleep(.5) # Don't be too aggressive...
  try:
    find_out = str(subprocess.check_output(["find", os.path.expanduser("~"), "-name", query]))
  except subprocess.CalledProcessError as e:
    find_out = str(e.output)
  find_array = find_out.split("\n")[:-1]
  if (len(find_array) == 0): return
  for i in xrange(min(5, len(find_array))):
    append_output(str(find_array[i]),"urxvt -e bash -c 'if [[ $(file "+find_array[i]+" | grep text) != \"\" ]]; then vim "+find_array[i]+"; else cd $(dirname "+find_array[i]+"); bash; fi;'");
  update_output()

def get_process_output(process, formatting, action):
  process_out = str(subprocess.check_output(process))
  if "%s" in formatting:
    out_str = formatting % (process_out)
  else:
    out_str = formatting
  if "%s" in action:
    out_action = action % (process_out)
  else:
    out_action = action
  return (out_str, out_action)

def get_xdg_cmd(cmd):

    import re

    try:
        import xdg.BaseDirectory
        import xdg.DesktopEntry
        import xdg.IconTheme
    except ImportError:
        return

    def find_desktop_entry(cmd):

        search_name = "%s.desktop" % cmd
        desktop_files = list(xdg.BaseDirectory.load_data_paths('applications',
                                                               search_name))
        if not desktop_files:
            return
        else:
            # Earlier paths take precedence.
            desktop_file = desktop_files[0]
            desktop_entry = xdg.DesktopEntry.DesktopEntry(desktop_file)
            return desktop_entry

    def get_icon(desktop_entry):

        icon_name = desktop_entry.getIcon()
        if not icon_name:
            return
        else:
            icon_path = xdg.IconTheme.getIconPath(icon_name)
            return icon_path

    def get_xdg_exec(desktop_entry):

        exec_spec = desktop_entry.getExec()
        # The XDG exec string contains substitution patterns.
        exec_path = re.sub("%.", "", exec_spec).strip()
        return exec_path

    desktop_entry = find_desktop_entry(cmd)
    if not desktop_entry:
        return

    exec_path = get_xdg_exec(desktop_entry)
    if not exec_path:
        return

    icon = get_icon(desktop_entry)
    if not icon:
        menu_entry = cmd
    else:
        menu_entry = "%%I%s%%%s" % (icon, cmd)

    return (menu_entry, exec_path)


special = {
    "bat": (lambda x: get_process_output("acpi", "%s", "")),
    "vi": (lambda x: ("vim","urxvt -e vim")),
}

while 1:
    userInput = sys.stdin.readline()
    userInput = userInput[:-1]

    # Clear results
    clear_output()

    # Kill previous worker threads
    if google_thr is not None:
        google_thr.terminate()
    if find_thr is not None:
        find_thr.terminate()

    # We don't handle empty strings
    if userInput == '':
        update_output()
        continue

    try:
        complete = subprocess.check_output("compgen -c %s" % (userInput),
                                               shell=True, executable="/bin/bash")
        complete = complete.split('\n')

        for cmd_num in range(min(len(complete), 5)):
                # Look for XDG applications of the given name.
                xdg_cmd = get_xdg_cmd(complete[cmd_num])
                if xdg_cmd:
                    append_output(*xdg_cmd)

    except:
        # if no command exist with the user input
        # but it can still be python or a special bash command
        pass

    finally:
        # Scan for keywords
        for keyword in special:
            if userInput[0:len(keyword)] == keyword:
                out = special[keyword](userInput)
                if out is not None:
                    prepend_output(*out)

        # Could be a command...
        append_output("execute '"+userInput+"'", userInput)

        # Could be bash...
        append_output("run '%s' in a shell" % (userInput),
                      "terminator -e %s" % (userInput))

        # Is this python?
        try:
            out = eval(userInput)
            if (type(out) != str and str(out)[0] == '<'):
                pass  # We don't want gibberish type stuff
            else:
                prepend_output("python: "+str(out),
                            "terminator -e python2.7 -i -c 'print "+userInput+"'")
        except Exception as e:
            pass

        # Spawn worker threads
        google_thr = Process(target=google, args=(userInput,))
        google_thr.start()
        find_thr = Process(target=find, args=(userInput,))
        find_thr.start()

        update_output()
