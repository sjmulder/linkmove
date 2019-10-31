linkmove
========

**linkmove** [-**v**] *source** ... **target*

Move files, leaving behind symlinks.

Description
-----------
Moves each *source* to *target*, follwing mv(1) semantics, leaving behind
symlinks at the source locations pointing to the new locations.

The moves are attempted using rename(2).  If that fails, the files or di‐
rectories are recursively copied and then deleted, making a best-effort
attempt to preserve metadata.

If **-v** is given, touched paths in *target* are output to standard error.

Rationale
---------
**linkmove** was created to move files in a website without breaking links.

Manually invoking mv(1) and ln(1) in sequence works well for smaller num‐
ber of items but it's tricky for a generic script to determine the proper
link target since the mv(1) *target* argument may refer to either the full
path or a parent directy.

Exit status
-----------
Zero on success, non-zero if an error occured.

Examples
--------
Rename and symlink old.txt to new.txt and verify the results:

   $ linkmove old.txt new.txt

   $ file -h old.txt new.txt
   old.txt: symbolic link to new.txt
   new.txt: ASCII text

Link-move multipe items with verbose output:

    $ linkmove foo bar /nfs
    /nfs/foo
    /nfs/foo/a
    /nfs/foo/b
    /nfs/bar

Here, the move was across filesystem boundaries so the foo directory was
copied recursively.

See also
--------
[ln(1)](http://man.openbsd.org/ln.1),
[mv(1)](http://man.openbsd.org/mv.1)

Authors
-------
Sijmen J. Mulder ⟨<ik@sjmulder.nl>⟩

Bugs
----
Trailing slashes in *source* paths cause an error.

Exits on any error, rather than continuing with the next file.
