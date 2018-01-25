# Aquilon Tools

This repository holds a collection of CLI tools for system administrators
working with the Aquilon Configuration Management Database. 

## aqnet ##

Shortcut for `aq show network device` with a tabular output format.

## aqc ##

Run any aq command adding colo[u]r to output.

## aqs ##

Shortcut for `aq show host` or `aq show cluster` with colo[u]ri[sz]ing.

## ckey ##

Adds colo[u]r to key/value pairs on stdin.

## diffprof ##

Find differences between many Quattor host profiles located in two
distinct directories.

## getprof ##

Downloads one or more host or cluster profiles from `AQD_HOST` over
HTTP or SSH.

## git-diffuniq / diffuniq ##

Distill git diff output to just the common set of changes.

## prdiff ##

Compares two Quattor host profiles and displays the differences with
resource path context maintained.

## razor ##

Multi-process recursive diff tool.  Works the same as `diff -r` but
supports comparing .gz files as well, and runs in a configurable
number of processes to speed up the processing of large directory trees.

## rifle (xmlnm) ##

Displays paths and values in Quattor object profile data from raw
.xml, .xml.gz, .json or .json.gz files.  The current Quattor host profile
may be used, a file may be supplied as an argument, or a profile for
a host or cluster may be downloaded from the broker (via getprof).

## trifle ##

Tiny fast version of rifle for mass conversion of JSON profiles.
Used by diffprof.

