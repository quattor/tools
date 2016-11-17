#!/bin/ksh
#
# Test that help messages can be displayed from all tools
#
CALL=$(basename $0)
CALLDIR=$(dirname $0)
BINDIR=$CALLDIR/../bin

typeset -i retval=0

for prog in aqnet aqs ckey diffprof diffuniq getprof git-diffuniq prdiff \
            razor rifle trifle xmlnm; do
    if [ `$BINDIR/$prog -h 2>&1 | grep -c "Syntax: $prog "` -eq 0 ]; then
        echo -------------------------------------------------------------- >&2
        echo ERROR: $prog -h did not return expected output >&2
        echo >&2
        $BINDIR/$prog -h
        retval=1
    fi
done
[ $retval -gt 0 ] && echo -------------------------------------------------------------- >&2
exit $retval
