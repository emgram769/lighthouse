#!/usr/bin/python3
import sys
import argparse as ag


def basic(sett):
    out = str()
    if sett.show_execute:
        out += "{Execute %%B'%s'%%|%s}" % (sett.user_input,
                                           sett.user_input)
    if sett.show_in_shell:
        out += "{Run the command in a shell %%B'%s'%%|%s -e %s}" % (sett.user_input,
                                                                 sett.term,
                                                                 sett.user_input
                                                                 )
    return out

if __name__ == "__main__":
    parser = ag.ArgumentParser()
    parser.add_argument("user_input")
    parser.add_argument("--show_execute", default=True, type=bool)
    parser.add_argument("--show_in_shell", default=True, type=bool)
    parser.add_argument("--term", default="urvxt", type=str)
    settings = parser.parse_args()
    print(basic(settings))
