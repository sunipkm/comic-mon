What actually ended up working

For reasons I do not understand, my system worked when I ran contrary to the (quite sensible) recommendation from @allquixotic.

1) Disable LIBGL_ALWAYS_INDIRECT

This wound up being really difficult, simply because I had to find where it was set.

In the folder \etc\profile.d was a file wsl-integration.sh.
The above file was actually a symlink; the actual file was /usr/share/wslu/wsl-integration.sh
In that file the variable LIBGL_ALWAYS_INDIRECT was set, so I commented out that line.
2) Do not use the -wgl command line argument (or its GUI equivalent) for VcXsrv

Since I was launching VcXsrv from a GUI client, this meant leaving the second option box, titles "Native opengl", unchecked.

Only once I had both those changes made (and did a fresh reboot to make sure no old settings for VcXsrv had persisted) then the gears in glxgears were turning at a normal rate, and I could reorient them using the arrow keys, just like they're supposed to work.
