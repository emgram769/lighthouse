#!/usr/bin/python2.7
import re
import subprocess
import sys


def get_xdg_cmd(cmd):

    try:
        import xdg.BaseDirectory
        import xdg.DesktopEntry
        import xdg.IconTheme
    except ImportError as e:
        print(e)
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


def find_xdg(query):
    """
    Look for XDG applications of the given name.
    """
    try:
        complete = subprocess.check_output("compgen -c %s" % (query),
                                           shell=True,
                                           executable="/bin/bash")
        complete = complete.split()

        res = str()
        for cmd_num in range(min(len(complete), 5)):
            xdg_cmd = get_xdg_cmd(complete[cmd_num])
            if xdg_cmd:
                res += "{%s|%s}" % (xdg_cmd[0], xdg_cmd[1])

    except:
        # if no command exist with the user input
        res = ''

    return res

if __name__ == "__main__":
    try:
        arg = sys.argv[1]
    except:
        exit()

    print(find_xdg(arg))
