# lighthouse
A simple flexible popup dialog to run on X.

# Installation

Build the binary.

    make

Copy it to some location in your $PATH.

    sudo cp lighthouse /usr/bin/lighthouse

Create config files. (This is important!)

    make config

# How to use
Lighthouse is a simple dialog that pipes whatever input you type into
the standard input of the executable specified by `cmd=[file]` in your
`lighthouserc`. The standard output of that executable is then used to
generate the results.

Syntax
---
The syntax of a result is simple.
`{ title | output }`
The `title` is displayed in the results and the `output` is written to standard out
when that result is selected.  A common use case would therefore be
`lighthouse | sh` and `output` would be some shell command.  Run `make config` and then
`lighthouse | sh` to see this in action.  The `title` will be `look! [input]` and the
`output` will be `[input]`, so you've effectively created a small one time shell prompt.
To create multiple results simply chain them together: `{ title1 | output1 }{ title2 | output2 }`

Other ways to use lighthouse
---
Because everything is handled through standard in and out, you can use pretty much any
executable.  If you want to use a python file `cmd.py`, simply point to it in `~/.config/lighthouse/lighthouserc`
by making the line `cmd=cmd.py`.  (Be sure to include #!/usr/bin/python at the top of your script!)

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
