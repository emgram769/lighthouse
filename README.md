# lighthouse
A simple flexible popup dialog to run on X. Demos: https://gfycat.com/WiltedBetterDorado https://u.teknik.io/pAk6A1.webm

(In the demo a hotkey is mapped to `lighthouse | sh` with `lighthouserc` using `cmd.py`, which is included in `config/lighthouse/` but not installed by `make config`. Explanation below.)
# Installation

Build the binary.

    make

Copy it to some location in your $PATH.

    sudo cp lighthouse /usr/bin/lighthouse

Create config files. (This is important!)

    make config
    
You may also need to make the `cmd` script executable.  (If you replace this script, be sure to make that exectuable as well.)

    chmod +x ~/.config/lighthouse/cmd

Dependencies
---

Arch:

    libpth
    libx11
    libxcb
    cairo
    libxcb-xkb
    libxcb-xinerama

Ubuntu:

    libpth-dev
    libx11-dev
    libx11-xcb-dev
    libcairo2-dev
    libxcb-xkb-dev
    libxcb-xineram0-dev

NixOS:

    nixos.pkgs.xlibs.libX11
    nixos.pkgs.xlibs.libxcb
    nixos.pkgs.xlibs.libxproto
    nixos.pkgs.cairo

# How to use
Typically you'll want to map a hotkey to run

    lighthouse | sh

Lighthouse is a simple dialog that pipes whatever input you type into
the standard input of the executable specified by `cmd=[file]` in your
`lighthouserc`. The standard output of that executable is then used to
generate the results.  A selected result (move with arrow keys to highlight
and then hit enter to select) will then have its `action`
printed to standard out (and in the case above, into the shell).

Syntax
---
The syntax of a result is simple.
`{ title | action }`
The `title` is displayed in the results and the `action` is written to standard out
when that result is selected.  A common use case would therefore be
`lighthouse | sh` and `action` would be some shell command.  Run `make config` and then
`lighthouse | sh` to see this in action.  The `title` will be `look! [input]` and the
`action` will be `[input]`, so you've effectively created a small one time shell prompt.
To create multiple results simply chain them together: `{ title1 | action1 }{ title2 | action2 }`

Other ways to use lighthouse
---
Because everything is handled through standard in and out, you can use pretty much any
executable.  If you want to use a python file `~/.config/lighthouse/cmd.py`, simply point to it in `~/.config/lighthouse/lighthouserc`
by making the line `cmd=~/.config/lighthouse/cmd.py`.  (Be sure to include `#!/usr/bin/python` at the top of your script!)  If you'd like some inspiration, check out the script in `config/lighthouse/cmd.py`.

Debugging your script
---
Run `lighthouse` in your terminal and look at the output.  If the script crahes you'll see its
standard error, and if it succeeds you'll see what lighthouse is outputting.  Check out
`config/lighthouse/cmd.py` for an example of how more complicated scripts should work.

Options
---
Check out the sample `lighthouserc` in `config/lighthouse`.  Copy it to your directory by
running `make config`.

TODO
---
Add image, alignment, colors and other formatting features to the results syntax.

BUGS
---
The cursor doesn't actually move the text backwards, making it hard to edit longer strings
