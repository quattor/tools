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
import re
import getopt
import signal
import tempfile
import copy
from subprocess import Popen, PIPE

CALL = os.path.basename(__file__)
CALLDIR = os.path.dirname(__file__)
CKEY = CALLDIR + "/ckey"
GETPROF = CALLDIR + "/getprof"
CCM_DIR = "/var/lib/ccm"
CCM_CURRENT_CID = CCM_DIR + "/current.cid"
CCM_PROFILE = CCM_DIR + "/profile.<CID>/profile"

##############################################################################

def usage():
    """ Displays a usage message. """

    print """
Syntax: %s [-ehIkv] [-o <output>] <file> [<resource_path ...>]
        %s [-ehIkv] [-o <output>] -c [<resource_path ...>]
        %s [-ehIkv] [-o <output>] -g {<host>|<alias>} [<resource_path ...>]
        %s [-ehIkv] [-o <output>] -G <cluster> [<resource_path ...>]

Displays paths in Quattor XML or JSON host profile data where

    -c          uses current profile for this host instead of requiring
                  a pathname to an XML or JSON file
    -e          removes Quattor-style escaping from output
                  (WARNING: use with care, this tool cannot know which
                   elements were escaped and which ones were not)
                  - a single -e unescapes path components only
                  - a double -ee unescapes values as well
    -G          downloads profile for named cluster/metacluster using getprof
    -g          downloads profile for named host using getprof tool
    -h          hides structural entries that do not have values
    -I          do not generate list index numbers, use a hash # instead
    -k          colourises output by piping through to ckey
    -o <output> send output to given file, instead of to stdout
    -p          if a value contains newlines, prefixes each newline
                  the resource path as well as the first line
    -v          display values only

    <file>      is the XML or JSON file to parse (may be plain or gzipped)
    <resource_path ...>
                one or more optional resource paths to filter by

Example:

    %s -c /software/components/spma

    %s /var/quattor/web/htdocs/profiles/aquilon20.one-nyp.ms.com.xml.gz \\
           /software/packages

    %s -g ilab901.one-nyp /metadata
""" % (CALL, CALL, CALL, CALL, CALL, CALL, CALL)
    return 1

##############################################################################

def unescape(s, path=False):
    """ Expand Quattor escape sequence. """
    if not do_unescape: return s
    if do_unescape < 2 and not path: return s

    #
    # If this is a path, process one path component at a time
    # so that we can enclose the component in braces { ... }
    # if an expansion occurred
    #
    if path:
        lst = s.split("/")
    else:
        lst = [s]

    new_s = ""
    for comp in lst:
        if path and (len(new_s) == 0 or new_s[-1] != "/"): new_s += "/"
        complst = re.split("(_[0-9a-f][0-9a-f])", comp)

        add_s = ""
        expanded = False
        for atom in complst:
            decode_atom = False
            if re.match("_[0-9a-f][0-9a-f]", atom):
                if path:
                    #
                    # Escaped characters in paths will only be unescaped
                    # if a printable character results and one that
                    # is likely to have been escaped (i.e. not letters)
                    #
                    i = int(atom[1:], 16)
                    if (i >= 0x20 and i <= 0x40) or \
                             (i >= 0x5b and i <= 0x60) or \
                             (i >= 0x7b and i <= 0x7e):
                        decode_atom = True
                else:
                    decode_atom = True

            if decode_atom:
                add_s += atom[1:].decode("hex")
                expanded = True
            else:
                add_s += atom

        if not path or not expanded:
            new_s += add_s
        else:
            new_s += "{" + add_s + "}"

    return new_s

##############################################################################

def chkwrite(output, s, xdup, xout):
    """ Write output but check for lines we've been asked to duplicate. """

    if xdup is not None:
        for m in xdup:
            if s[0:m[1]] == m[0]:
                xout.write(s)

    output.write(s)

##############################################################################

def walk_xml_tree(root, idx=[], strip_prof=False,
                  output=sys.stdout, xdup=None, xout=None):
    """ Walk XML tree and output resource paths of interest. """
    name = root.get('name')
    if not name:
        if gen_indices:
            name = '/' + str(idx[-1])
        else:
            name = '/#'
    else: name = '/' + name

    text = root.text
    if not text: text = ''
    text = text.strip()

    rpath = name
    i = -1
    for node in root.iterancestors():
        i -= 1
        s = node.get('name')
        if s == None:
            if gen_indices:
                s = str(idx[i])
            else:
                s = '#'

        rpath = '/' + s + rpath

    s = ""
    if (not hide_terminals or text) and not value_only:
        pathname = unescape(rpath.encode("utf-8"), True)
        s += pathname
    if text:
        if not value_only: s += " = "
        s += unescape(text.strip().encode("utf-8")) + "\n"
    elif not hide_terminals:
        s += "\n"

    if s:
        if strip_prof and s[:9] == "/profile/": s = s[8:]
        if not prefix_newlines:
            chkwrite(output, s, xdup, xout)
        else:
            lines = s.splitlines()
            s = lines.pop(0)
            chkwrite(output, s + "\n", xdup, xout)
            for line in lines:
                s2 = pathname + " .= " + line + "\n"
                if strip_prof and s2[:9] == "/profile/": s2 = s2[8:]
                output.write(s2)

        output.flush()

    this_idx = 0
    for sub in root.getchildren():
        new_idx = copy.copy(idx)
        new_idx.append(this_idx)
        this_idx += 1

        walk_xml_tree(sub, new_idx, strip_prof,
                      output=output, xdup=xdup, xout=xout)

def walk_dict(d, root="", node=None,
              output=sys.stdout, xdup=None, xout=None):
    """ Walk dictionary and output resource paths of interest. """
    if root == "/": root = ""

    for key in sorted(d):
        if node is not None and key != node: continue

        path = unescape(root + "/" + key.encode("utf-8"), True)

        if type(d[key]) is unicode:
            value = unescape(d[key].encode("utf-8"))
            if not value_only:
                if "\n" in value and prefix_newlines:
                    chkwrite(output, path + " = " +
                                 ("\n" + path + " .= ").join(
                                             value.splitlines()) + "\n",
                             xdup, xout)
                else:
                    chkwrite(output, path + " = " + value + "\n", xdup, xout)
            else:
                output.write(value + "\n")
        elif type(d[key]) is dict:
            if not hide_terminals and not value_only:
                chkwrite(output, path + "\n", xdup, xout)

            walk_dict(d[key], root=path,
                      output=output, xdup=xdup, xout=xout)
        elif type(d[key]) is list:
            for i in xrange(0, len(d[key])):
                if gen_indices:
                    lpath = path + "/" + str(i)
                else:
                    lpath = path + "/#"

                if type(d[key][i]) is unicode:
                    value = unescape(d[key][i].encode("utf-8"))
                    if not value_only:
                        if "\n" in value and prefix_newlines:
                            chkwrite(output, lpath + " = " +
                                         ("\n" + lpath + " .= ").join(
                                                     value.splitlines()) + "\n",
                                     xdup, xout)
                        else:
                            chkwrite(output, lpath + " = " + value + "\n",
                                     xdup, xout)
                    else:
                        output.write(value + "\n")
                elif type(d[key][i]) is dict:
                    if not hide_terminals and not value_only:
                        chkwrite(output, lpath + "\n", xdup, xout)

                    walk_dict(d[key][i], root=lpath,
                              output=output, xdup=xdup, xout=xout)

##############################################################################

def current_profile():
    """ Return name of current host profile. """
    if debug: sys.stderr.write("%s: locating current profile in %s\n" % \
                                        (CALL, CCM_DIR))

    with open(CCM_CURRENT_CID, "r") as f:
        cid = f.read().strip()

    filename = CCM_PROFILE.replace("<CID>", cid)
    if os.path.exists(filename + ".json"):
        return filename + ".json"

    return filename + ".xml"

def get_profile(host, cluster = False):
    """ Download profile to temporary file and return tempfile handle. """
    cmd = [GETPROF, host]
    if cluster: cmd.insert(1, "-C")
    if debug:
        cmd.insert(1, "-D")
        sys.stderr.write("%s: launching '%s'\n" % (CALL, " ".join(cmd)))

    tempfh = tempfile.NamedTemporaryFile(prefix="tmp.%s." % CALL)
    pipe = Popen(cmd, stdout=tempfh)
    rc = pipe.wait()
    if rc != 0: raise RuntimeError("'%s' returned exit status %d" % \
                                        (" ".join(cmd), rc))
    return tempfh

def get_xml_elements(tree, path):
    """ Return elements in a particular XML path. """
    xpath = ''
    for comp in path.split('/')[1:]:
        if comp == '*' or comp == '': xpath += '/*[@name]'
        else: xpath += '/*[@name="%s"]' % comp

    if debug: sys.stderr.write("%s: searching for XML elements: %s\n" % \
                                                            (CALL, xpath))

    return tree.xpath(xpath)

##############################################################################

def main(args=sys.argv, outfile=sys.stdout, xdup=None, xout=None):
    """
    Main program entry point.  If run as 'rifle', then all of the default
    parameters are used.  Otherwise, parameters may be overridden:

           args = list of command-line arguments
        outfile = file object to write the output
           xdup = optional list of resource paths to duplicate
           xout = optional file object to write duplicated resource paths to
    """
    global debug, hide_terminals, value_only, do_unescape
    global prefix_newlines, gen_indices

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
        opts, args = getopt.getopt(args[1:], "cDeG:g:hIko:pv")
    except getopt.GetoptError as err:
        print "%s: %s" % (CALL, str(err))
        return 1

    debug = hide_terminals = value_only = False
    prefix_newlines = ckey = outopen = False
    gen_indices = True
    do_unescape = 0
    fname = None
    for o, a in opts:
        if o == "-c": fname = current_profile()
        elif o == "-D": debug = True
        elif o == "-e": do_unescape += 1
        elif o == "-G":
            tempfh = get_profile(a, cluster = True)
            fname = tempfh.name
        elif o == "-g":
            tempfh = get_profile(a)
            fname = tempfh.name
        elif o == "-h": hide_terminals = True
        elif o == "-I": gen_indices = False
        elif o == "-k": ckey = True
        elif o == "-o":
            outfile = open(a, "w")
            outopen = True
        elif o == "-p": prefix_newlines = True
        elif o == "-v": value_only = True

    if fname == None:
        if len(args) < 1: return usage()
        fname = args[0]
        args.pop(0)

    if not os.path.exists(fname):
        sys.stderr.write("%s: file not found: %s\n" % (CALL, fname))
        return 1

    if xdup is None:
        xout = None
    else:
        #
        # Normalise xdup list
        #
        newdup = []
        for m in xdup:
            newdup.append((m + " ", len(m)+1))

        xdup = newdup

    #
    # Redirect stdout to ckey if -k was given
    #
    if ckey:
        pipe = Popen([CKEY], stdin=PIPE, stdout=outfile)
        output = pipe.stdin
    else:
        pipe = None
        output = outfile

    #
    # Process file
    #
    if debug: sys.stderr.write("%s: opening %s\n" % (CALL, fname))
    if fname[-5:] != ".json" and fname[-8:] != ".json.gz":
        #
        # Parse XML
        #
        try:
            import ms.version
            ms.version.addpkg('lxml', '2.3.2')
        except:
            pass

        from lxml import etree

        try:
            tree = etree.parse(fname)
        except Exception as err:
            print "%s: %s: %s" % (CALL, str(err), fname)
            return 1

        if len(args) == 0:
            root = tree.getroot()
            walk_xml_tree(root, strip_prof=True,
                          output=output, xdup=xdup, xout=xout)
        else:
            for path in args:
                if path[0] != '/': path = '/' + path
                elst = get_xml_elements(tree, path)
                strip_prof = True
                if len(elst) == 0 and path[:9] != "/profile/":
                    path = "/profile" + path
                    elst = get_xml_elements(tree, path)
                elif path[:9] == "/profile/":
                    strip_prof = False

                for root in elst:
                    walk_xml_tree(root, strip_prof=strip_prof,
                                  output=output, xdup=xdup, xout=xout)
    else:
        #
        # Parse JSON
        #
        import json

        if fname[-3:] == ".gz":
            import gzip
            f = gzip.open(fname)
        else:
            f = open(fname)

        try:
            jsdata = json.load(f)
            if len(args) == 0:
                #
                # Display entire file
                #
                walk_dict(jsdata, output=output, xdup=xdup, xout=xout)
            else:
                #
                # Display only specific paths, first check to see if any
                # paths use wildcards and expand those now
                #
                new_args = []
                lpath = ""
                for path in args:
                    if path[-2:] == "/*": path = path[:-2]
                    if "*" in path:
                        if path[0] == "/": path = path[1:]
                        d = jsdata
                        lst = path.split("/")
                        path_found = True
                        if len(lst) > 1:
                            for comp in lst[:-1]:
                                if comp == "*":
                                    for comp in d:
                                        rpath = path[len(lpath)+2:]
                                        args.append("%s/%s/%s" % \
                                                        (lpath, comp, rpath))
                                else:
                                    if comp not in d:
                                        path_found = False
                                    else:
                                        d = d[comp]
                                        lpath += "/" + comp
                    else:
                        new_args.append(path)

                #
                # Walk tree for each expanded path
                #
                for path in new_args:
                    if path[0] == "/": path = path[1:]
                    d = jsdata
                    lst = path.split("/")
                    path_found = True
                    if len(lst) > 1:
                        for comp in lst[:-1]:
                            if comp not in d: path_found = False
                            else: d = d[comp]

                    if path_found and type(d) is dict:
                        walk_dict(d, root="/" + "/".join(lst[:-1]),
                                  node=lst[-1], output=output)

        finally:
            f.close()

    if pipe: pipe.communicate('')

    if outopen: outfile.close()
    return 0

if __name__ == "__main__":
    retval = main()
    exit(retval)
