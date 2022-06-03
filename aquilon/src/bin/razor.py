#!/usr/bin/env python
##############################################################################
#
# See COPYRIGHT file in source distribution for copyright holders
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
##############################################################################

import sys
import os
import getopt
import re
import signal
from multiprocessing import cpu_count, Pool, Value, Lock
import math
import subprocess
import gzip
from tempfile import NamedTemporaryFile
from time import sleep

CALL = os.path.basename(__file__)
CALLDIR = os.path.dirname(__file__)

try:
    import ms.version
    ms.version.addpkg("tqdm", "3.8.0")
except:
    pass

from tqdm import tqdm

stderr_lock = Lock()

##############################################################################

def syntax():
    """ Displays command syntax. """
    print """\
Syntax: %s [-DdGg] [-i <include>] [-o <output>] [-p <workers>] \\
                            [-x <exclude>] [-- <diff-options>] <dir1> <dir2>
        %s -h\
""" % (CALL, CALL)

def usage():
    """ Displays a usage message. """

    syntax()
    print """
Multi-process recursive diff tool with support for compressed files, where

    -D    turn on debugging.
    -d    files with .gz suffix will be decompressed in pipeline prior to
          the diff.
    -G    don't use a progress bar in non-debug mode.
    -g    use a Unicode progress bar instead of the ASCII # symbol.
    -h    display this help message.
    -i <include>
          RE to match filenames to include, multiple -i options are allowed.
    -o <output>
          send output to given file, instead of to stdout
    -p <workers>
          maximum number of worker processes to use, default is the number
          of CPU cores.
    -x <exclude>
          RE to match filenames to exclude, multiple -x options are allowed.
          In case of a clash, these take precedence over the REs specified
          by the -i option.

  <diff-options>
          additional options to pass to each individual diff command.
  <dir1>  first directory containing files and directories to compare.
  <dir2>  second directory containing files and directories to compare against.
"""

##############################################################################

def chunks(l, n):
    """ Yield successive n-sized chunks from l. """
    for i in xrange(0, len(l), n):
        yield l[i:i+n]

def gunzip_to_temp(fname):
    """ Decompress a gzip'ed file to a temporary file and return the
        temporary file handle. """

    outf = NamedTemporaryFile(prefix="tmp.%s." % CALL)

    with gzip.open(fname) as f:
        outf.write(f.read())

    outf.flush()
    outf.seek(0)
    return outf

def diff_worker(filelist):
    """ Worker responsible for calling diff on a given set of files. """

    pid = os.getpid()

    if debug:
        with stderr_lock:
            sys.stderr.write(("%s: DEBUG: pid %d: worker thread started with "
                              "%d file(s) to diff\n") %
                                (CALL, pid, len(filelist)))

    all_output = []

    try:
        for f in filelist:
            (file1, file2) = f

            tmp1 = tmp2 = None
            tfile1 = file1
            tfile2 = file2

            if decompress:
                for gzfile in (file1, file2):
                    if gzfile[-3:] == ".gz":
                        if debug:
                            with stderr_lock:
                                sys.stderr.write("%s: DEBUG: pid %d: "
                                                 "decompressing: %s\n" %
                                                            (CALL, pid, gzfile))

                        tmp = gunzip_to_temp(gzfile)
                        tfile = tmp.name
                        if gzfile == file1:
                            tmp1 = tmp
                            tfile1 = tfile
                        else:
                            tmp2 = tmp
                            tfile2 = tfile

            cmd = ["diff"] + diffopt + [tfile1, tfile2]

            if debug:
                with stderr_lock:
                    sys.stderr.write("%s: DEBUG: pid %d: executing: %s\n" %
                                                    (CALL, pid, " ".join(cmd)))

            try:
                out = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                if e.returncode == 1:
                    #
                    # Return code 1 from diff just means the files differed
                    #
                    out = e.output
                    with diff_status.get_lock():
                        if diff_status.value == 0:
                            diff_status.value = 1
                else:
                    #
                    # A return code >1 from diff is a problem
                    #
                    out = ""
                    if debug:
                        with stderr_lock:
                            sys.stderr.write("%s: DEBUG: pid %d: '%s' returned "
                                             "status %d: %s\n" %
                                                 (CALL, pid, " ".join(e.cmd),
                                                  e.returncode, e.output))
                    with diff_status.get_lock():
                        diff_status.value = 2

            if out != "":
                all_output.append("diff -r %s %s\n%s" % (file1, file2, out))

            if tmp1 is not None: tmp1.close()
            if tmp2 is not None: tmp2.close()

            with counter.get_lock():
                counter.value += 1

    except KeyboardInterrupt:
        diff_status.value = 2
        if debug:
            with stderr_lock:
                sys.stderr.write("%s: DEBUG: pid %d: interrupted by "
                                 "Ctrl+C\n" % (CALL, pid))

    return all_output

def run_diff(numworkers, outfile, dir1, dir2):
    """ Find recursive differences in the two given directories. """
    global counter, diff_status

    filelist = []
    got_file = set()
    for thisdir in (dir1, dir2):
        if debug:
            sys.stderr.write("%s: DEBUG: finding files in %s\n" %
                                                            (CALL, thisdir))

        for dirpath, dirs, files in os.walk(thisdir):
            for f in dirs + files:
                f = os.path.join(dirpath, f)
                if len(re_include) > 0:
                    #
                    # Check that filename is explicitly included
                    # (if an include list was provided)
                    #
                    ok = False
                    for r in re_include:
                        if r.search(f):
                            ok = True
                            break

                    if not ok: continue

                ok = True
                for r in re_exclude:
                    #
                    # Check that filename is not explicitly excluded
                    #
                    if r.search(f):
                        ok = False
                        break

                if not ok: continue

                if thisdir == dir1:
                    otherdir = dir2
                else:
                    otherdir = dir1

                subdir = os.path.dirname(f[len(thisdir)+1:])
                if subdir != "": otherdir += "/" + subdir
                thisbase = os.path.basename(f)
                otherfile = otherdir + "/" + thisbase

                #
                # Is the file in the other directory?
                #
                if thisdir == dir1:
                    if os.path.exists(otherfile):
                        #
                        # Exists in both locations, we will run diff on this
                        # (if it is a file)
                        #
                        if os.path.isfile(f):
                            filelist.append((f, otherfile))

                        got_file.add(f)
                    else:
                        #
                        # Exists in dir1, not in dir2
                        #
                        outfile.write("Only in %s: %s\n" % (dirpath, thisbase))
                elif otherfile not in got_file:
                    #
                    # Exists in dir2, not in dir1
                    #
                    outfile.write("Only in %s: %s\n" % (dirpath, thisbase))

    fc = len(filelist)
    if fc == 0: return 0
    if numworkers > fc: numworkers = fc

    #
    # Start worker processes
    #
    counter = Value('i', 0)       # global counter for progress bar
    diff_status = Value('i', 0)   # exit status of diff
    last_count = 0
    if debug:
        sys.stderr.write("%s: DEBUG: starting %d worker process(es) "
                         "to diff %d file(s)\n" % (CALL, numworkers, fc))

    pool = Pool(numworkers)
    async = pool.map_async(diff_worker, chunks(filelist,
                                int(math.ceil((fc + 0.0) / numworkers))))

    #
    # Watch counter progress and use progress bar
    #
    try:
        with tqdm(total=len(filelist), ascii=not progress_unicode,
                  unit="files", disable=not progress_bar) as pbar:
            while not async.ready():
                sleep(0.2)

                with counter.get_lock():
                    new_count = counter.value

                if new_count > last_count:
                    pbar.update(new_count - last_count)
                    last_count = new_count
    except KeyboardInterrupt:
        sys.stderr.write("%s: interrupted by Ctrl+C\n" % CALL)
        pool.close()
        return 3

    results = async.get()
    outfile.write("".join(["".join(l) for l in results]))
    pool.close()

    if not async.successful():
        raise RuntimeError("Some files failed to be processed by diff")

    return diff_status.value

##############################################################################

def main(args=sys.argv, outfile=sys.stdout):
    global debug, re_include, re_exclude, diffopt, decompress
    global progress_bar, progress_unicode

    if args == sys.argv:
        #
        # Can only use signal() if this is the main thread, and not a
        # module function executed from another program
        #
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)

    #
    # Parse command-line arguments
    #
    try:
        opts, args = getopt.getopt(args[1:], "DdGghi:o:p:x:")
    except getopt.GetoptError as err:
        print "%s: %s" % (CALL, str(err))
        return 2

    debug = decompress = outopen = False
    diffopt = []
    re_include = []
    re_exclude = []
    numworkers = cpu_count()
    progress_bar = True
    progress_unicode = False

    for o, a in opts:
        if o == "-d": decompress = True
        elif o == "-D": debug = True
        elif o == "-G": progress_bar = False
        elif o == "-g": progress_unicode = True
        elif o == "-h":
            usage()
            return 2
        elif o == "-i": re_include.append(re.compile(a))
        elif o == "-o":
            outfile = open(a, "w")
            outopen = True
        elif o == "-p": numworkers = int(a)
        elif o == "-x": re_exclude.append(re.compile(a))

    if len(args) < 2:
        syntax()
        return 2

    if len(args) > 2:
        diffopt += args[:len(args)-2]
        args = args[len(args)-2:]

    if debug: progress_bar = False

    dir1 = args[0]
    dir2 = args[1]
    for thisdir in (dir1, dir2):
        if not os.path.isdir(thisdir):
            sys.stderr.write("%s: directory not found: %s\n" % (CALL, thisdir))
            return 2
        if not os.access(thisdir, os.R_OK) or not os.access(thisdir, os.X_OK):
            sys.stderr.write("%s: no permission to access directory: %s\n" %
                                                            (CALL, thisdir))
            return 2

    try:
        retval = run_diff(numworkers, outfile, dir1, dir2)

    finally:
        if outopen: outfile.close()

    return retval

if __name__ == "__main__":
    retval = main()
    exit(retval)
