#!/bin/ksh
#
# Runs diffprof with a variety of options and compares output
# with previously saved output
#
# Note that this also exercises the rifle, trifle, razor and
# git-diffuniq tools
#
CALL=$(basename $0)
CALLDIR=$(dirname $0)
DIFFPROF=$CALLDIR/../bin/diffprof
TMPDIR=$(mktemp -d /tmp/tmp.$CALL.XXXXXXXXXX)

trap 'Exit 1' INT HUP TERM QUIT

function Exit
{
    rm -fr $TMPDIR
    exit $*
}

#
# Report diffprof command failure
#
function cmd_failed
{
    echo failed
    echo ERROR: diffprof command returned non-zero exit status:
    cat $TMPDIR/diffprof.err
    Exit 1
}

#
# Run diff to compare diffprof output with expected output,
# except if there is more than one hunk in the expected output,
# in which case we cannot guarantee order so the best we can
# do is count the number of lines and check they match
#
function line_diff
{
    if [ $(grep -c '^@@ Difference' $2) -gt 1 ]; then
        typeset -i lines1=$(wc -l < $1)
        typeset -i lines2=$(wc -l < $2)
        if [ $lines1 -ne $lines2 ]; then
            echo ERROR: line_diff: $lines1 does not match $lines2 >&2
            cat $1
            echo ------------------------------------------------------------------------------
            cat $2
            return 1
        fi
        return 0
    else
        diff $*
    fi
}

#
# Report diff command failure
#
function diff_failed
{
    echo failed
    echo ERROR: diff did not return expected output
    cat $TMPDIR/diff.err
    Exit 1
}

#
# Report file did not exist
#
function file_failed
{
    echo failed
    echo ERROR: expected file $1 does not exist
    Exit 1
}

#
# Check that there were zero differences
#
function check_empty
{
    typeset -i failed=0
    if [ $(wc -l < $1) -ne 1 ]; then
        failed=1
    else
        if [ $(grep -c 'file is empty (no differences)' $1) -ne 1 ]; then
            failed=1
        fi
    fi
    if [ $failed -eq 1 ]; then
        echo failed
        echo ERROR: differences found when none were expected:
        cat $1
        exit 1
    fi
}

debug=
debugnl=
if [ "x$1" = x-D ]; then
    debug="-Dp1 "
    debugnl="\n"
fi

#
# Test diffprof with a variety of options and both
# XML and JSON files
#
for ftype in json xml; do
    for dtype in diff nodiff; do
        for args in -G -Gx -Gax; do
            [ -n "$debug" ] && echo ------------------------------------------------------------------------------
            printf "$CALL: executing diffprof $debug$args $ftype $dtype test ... $debugnl"
            $DIFFPROF $debug$args $CALLDIR/diffprof/$ftype/$dtype/1 $CALLDIR/diffprof/$ftype/$dtype/2 > $TMPDIR/diffprof$args.out 2> $TMPDIR/diffprof.err || cmd_failed
            [ -n "$debug" ] && cat $TMPDIR/diffprof.err
            if [ $dtype = diff ]; then
                line_diff $TMPDIR/diffprof$args.out $CALLDIR/diffprof/diffprof$args-$ftype.out > $TMPDIR/diff.err 2>&1 || diff_failed
            else
                check_empty $TMPDIR/diffprof$args.out
            fi
            echo success
        done

        #
        # Test the in-place feature (-x) separately
        #
        mkdir -p $TMPDIR/inplace || Exit 1
        cp -Lr $CALLDIR/diffprof/$ftype/$dtype/1 $CALLDIR/diffprof/$ftype/$dtype/2 $TMPDIR/inplace/. || Exit 1
        args=-Gaix
        [ -n "$debug" ] && echo ------------------------------------------------------------------------------
        printf "$CALL: executing diffprof $debug$args $ftype $dtype test ... $debugnl"
        $DIFFPROF $debug$args $TMPDIR/inplace/1 $TMPDIR/inplace/2 > $TMPDIR/diffprof$args.out 2> $TMPDIR/diffprof.err || cmd_failed
        [ -n "$debug" ] && cat $TMPDIR/diffprof.err
        if [ $dtype = diff ]; then
            line_diff $TMPDIR/diffprof$args.out $CALLDIR/diffprof/diffprof$args-$ftype.out > $TMPDIR/diff.err 2>&1 || diff_failed
        else
            check_empty $TMPDIR/diffprof$args.out
        fi
        for f in $TMPDIR/inplace/1/*.$ftype.gz $TMPDIR/inplace/2/*.$ftype.gz; do
            f=${f%.${ftype}.gz}.txt.gz
            [ -f $f ] || file_failed $f
        done
        echo success

        rm -fr $TMPDIR/inplace/1 $TMPDIR/inplace/2
    done
done

Exit 0
