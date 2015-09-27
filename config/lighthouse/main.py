#!/usr/bin/python3

import multiprocessing as mp
import shlex
import json
import subprocess as sp
import sys
import os
import signal


class LocationNotFoundError(Exception):
    def __init__(self, dict_struct=None):
       print()


def print_out(results):
    print("".join([x if x is not None else "" for x in results]))


def clean_subprocesses(processList, subprocess_list, _exit=0):
    """
    Stop all processes started.
    """
    for p in processList:
        p.terminate()
        del p
    for p in subprocess_list:
        p.terminate()
        del p
    if _exit:
        exit()


def get_result(subprocess, results_array, i):
    """
    """
    results = subprocess.stdout.read().decode('utf-8').replace("\n", "")
    if results.strip() != '':
        results_array[i] = results
        print_out(results_array)


def make_commands(scripts_dict):
    """
    Take the script characteristic:
        {"location": "...",
         "foo": "bar"
         }
    To transform it like this:
        "location" --foo bar
    So remote script can parse the arguments.
    """
    res = []
    for script_characteristics in scripts_dict:
        program_name = None
        program_flags = str()
        for charac, arg in script_characteristics.items():
            if charac == "location":
                program_name = os.path.expanduser(arg)
            else:
                program_flags += "--%s %s " % (charac, arg)
        if program_name is None:
            # Script location should be always specified by the user.
            raise LocationNotFoundError
        res.append("%s %s" % (program_name, program_flags))

    return res


if __name__ == "__main__":
    subprocess_array = []
    scripts_file = open(os.path.expanduser("~/.config/lighthouse/scripts.json"))
    scripts = json.loads(scripts_file.read())
    commands = make_commands(scripts)
    processList = []
    manager = mp.Manager()
    subprocess_list = manager.list()
    signal.signal(signal.SIGTERM, lambda x, y: clean_subprocesses(processList, subprocess_list, 1))

    while 1:
        request = sys.stdin.readline()[:-1]
        clean_subprocesses(processList, subprocess_list)
        processList = []
        subprocess_list = manager.list()

        if len(request.strip()) == 0:
            print('')
            continue

        results_array = manager.list([None for x in range(len(scripts))])
        process_array = []

        for i, script in enumerate(commands):
            cmd = "%s '%s'" % (script, request)
            args = shlex.split(cmd)
            subprocess = sp.Popen(args, stdout=sp.PIPE)
            process_array.append(subprocess)

            process = mp.Process(target=get_result,
                                 args=(subprocess,
                                       results_array,
                                       i
                                       )
                                 )
            process.start()
            processList.append(subprocess)
