/*****************************************************************************
 *
 * trifle.c
 * Author: Mark R. Bannister
 *
 * Use of this software is subject to the Apache 2 License:
 * http://www.opensource.org/licenses/apache2.0
 *
 * See source code for the COPYRIGHT file.
 *
 *****************************************************************************
 *
 * Converts Quattor JSON profiles to resource path format
 * Tiny & fast version of rifle with JSON support only (RFC 7159)
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <limits.h>
#include <libgen.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <zlib.h>

/* Global variables */
char *prog;            /* program name */
int gzlevel=0;         /* compress output files and at what level? (-c, -1-9) */
int debug=0;           /* debug level (-D) */
char *outdir=NULL;     /* alternative output directory (-d) */
char unescape=0;       /* unescape path components? (-e) */
char force=0;          /* forcibly create output files even if newer (-F) */
char filelist=0;       /* input filenames are files containing lists of files to
                          process, not the JSON profiles themselves (-f) */
char terminals=1;      /* display structural entries with no values? (-h) */
char gen_index=1;      /* generate list index numbers? (-I) */
char multiline=0;      /* split multi-line values onto multiple lines? (-n) */
char *suffix=".txt";   /* suffix for output filenames (-O) */
char multiprefix=0;    /* if splitting multi-line values, prefix each new line
                          with a repeat of the resource path? (-p) */
char *strip=".json";   /* suffix to remove from output filenames (-R) */
char *slice=".slice";  /* suffix to use for slice filenames (-S) */
char *slices=NULL;     /* buffer to hold list of resource paths to slice (-s) */
char to_stdout=0;      /* send output to stdout if only one JSON profile
                          is given on the command-line (-t) */
char from_stdin=0;     /* input comes from stdin if only one filename is
                          given on command line and it is "-" */

const char *current_file;  /* current input filename */
unsigned long linenum;     /* current line number in input file */

size_t len_outdir;    /* length of outdir string */
size_t len_suffix;    /* length of suffix string */
size_t len_strip;     /* length of strip suffix string */
size_t len_slice;     /* length of slice suffix string */
size_t size_slices;   /* length of slices buffer */
int num_slices=0;     /* number of strings in slices buffer */
char gzmode[3]="wbX"; /* mode argument for gzopen() when writing */

static volatile char terminate=0;  /* set by signal handler if we need to exit
                                      in a hurry */

/*
 * MAXBUF will limit the maximum length of any given resource path
 */
#define MAXBUF 4096

/*
 * MAX_SLICES limits the total number of slices that can be requested
 */
#define MAX_SLICES 1000

/*
 * GZ_BUFFER_SIZE is the internal buffer size for compression/decompression
 */
#define GZ_BUFFER_SIZE 524288

/*
 * start_slice() will open a slice file if the resource path
 * being output is one of the named paths to slice and write the
 * path name and the first character of the value
 */
#define start_slice(ch) (num_slices>0 && is_slice(path) ? \
                          ((slicefp=fopen(slicefile, "w")) ? \
                              fprintf(slicefp, "%s = %c", path, ch) : 0) : 0)

/*
 * stop_slice() will finish off and close a slice file if it has been opened
 */
#define stop_slice()    (slicefp ? \
                          (putc('\n', slicefp), \
                           fclose(slicefp), \
                           slicefp=NULL) : 0)

/*
 * write_slice() will add the next character to the open slice file,
 * if this value is being captured to the file
 */
#define write_slice(ch) (slicefp ? putc(ch, slicefp) : 0)

/*
 * putvalue() puts a character to the output stream that is part of a resource
 * value - if the resource path has not been output yet, also writes this
 */
#define putvalue(ch)  (valueout ? (write_slice(ch), \
                         (to_stdout ? putchar(ch) : \
                             (outcompress ? \
                               gzputc(gzout, ch) : \
                               putc(ch, outfp)))) \
                      : (valueout=1, start_slice(ch), \
                          outcompress ? \
                               gzprintf(gzout, "%s = %c", path, ch) : \
                               fprintf(outfp, "%s = %c", path, ch)))

/*
 * gz_strerror() will return the zlib error message related to the given
 * zlib error number
 */
#define gz_strerror(errnum) \
              errnum==Z_ERRNO ? strerror(errno) : gzerror(gzin, &errnum)

/*****************************************************************************
 * sigterm()
 *
 * Set global terminate flag if a signal we are interested in is received
 *****************************************************************************/
void sigterm(int n)
{
    if (debug) fprintf(stderr, "%s: caught signal, terminating early\n", prog);
    terminate=1;
}

/*****************************************************************************
 * str_unescape()
 *
 * Unescape Quattor path component
 *
 * Returns pointer to unescaped path, or NULL if an error occurred
 *****************************************************************************/
static char *str_unescape(const char *s, char *buf, int buflen)
{
    const char *p;
    char *q, score=0, any_unescaped=0;
    int len;

    buflen-=3;  /* space for surrounding { } if we unescape anything, and \0 */

    /*
     * Loop through string, note that we do one loop iteration even when
     * the terminating NUL character is found so that we have the
     * opportunity to output an incomplete escape sequence that we
     * might have been in the middle of processing
     */
    for (p=s, q=buf+1, len=0; len<buflen; p++)
    {
        if (*p=='_') score=1;
        else if (score)
        {
            char convert=0;

            if (isxdigit(*p))
            {
                if (score==1) score++;
                else
                { /* found a possible escape sequence */
                    unsigned long n;
                    char hex[3];

                    hex[0]=*(p-1);
                    hex[1]=*p;
                    hex[2]='\0';
                    n=strtoul(hex, NULL, 16);
                    if ((n>=0x20 && n<=0x40) ||
                            (n>=0x5b && n<=0x60) ||
                            (n>=0x7b && n<=0x7e))
                    {
                        *(q++)=n;
                        len++;
                        convert=1;
                        any_unescaped=1;
                        score=0;
                    }
                    else convert=2;
                }
            }
            else convert=2;

            if (convert==2)
            {
                if (len+score+1>=buflen) goto unescape_overflow;
                *(q++)='_';
                if (score==2) *(q++)=*(p-1);
                if (*p!='\0') *(q++)=*p;
                len+=score+1-(*p=='\0');
                score=0;
            }
        }
        else
        {
            if (*p!='\0')
            {
                *(q++)=*p;
                len++;
            }
        }
        if (*p=='\0') break;
    }
    if (*p!='\0') goto unescape_overflow;
    len++;
    if (any_unescaped)
    {
        buf[0]='{';
        buf[len]='}';
        buf[len+1]='\0';
        return buf;
    }
    buf[len]='\0';
    return buf+1;

unescape_overflow:
    fprintf(stderr, "%s: error reading '%s': path name component exceeded "
                        "%d bytes at line %ld\n", prog, current_file,
                                                        buflen, linenum);
    return NULL;
}

/*****************************************************************************
 * find_last_slash()
 *
 * Finds last slash character in a given resource path, working around
 * unescaped components if necessary
 *
 * Returns pointer to last slash, or NULL if there is no slash
 *****************************************************************************/
static char *find_last_slash(const char *s)
{
    const char *p, *q=NULL;
    char escaped=0;

    for (p=s; *p!='\0'; p++)
    {
        if (unescape && *p=='{') escaped=1;
        else if (escaped && *p=='}') escaped=0;
        else if (*p=='/' && !escaped) q=p;
    }
    return (char *)q;
}

/*****************************************************************************
 * path_append()
 *
 * Add new component to end of path name
 *
 * Returns >0 if an error occurred
 *****************************************************************************/
static int path_append(const char *s, char *path, size_t *pathlen)
{
    char buf[MAXBUF];
    size_t len;

    if (debug>1) fprintf(stderr, " - path_append(%s, %s, %ld) at %s:%ld\n",
                                    s, path, *pathlen, current_file, linenum);

    if (unescape) s=str_unescape(s, buf, MAXBUF);
    if ((len=strlen(s))==0) return 0;

    if (*pathlen+len+2 > MAXBUF)
    {
        fprintf(stderr, "%s: error reading '%s': path name exceeded "
                            "%d bytes at line %ld\n", prog, current_file,
                                                            MAXBUF, linenum);
        return 1;
    }
    strcat(path, "/");
    strcat(path, s);
    *pathlen+=len+1;
    return 0;
}

/*****************************************************************************
 * path_remove()
 *
 * Remove last component from end of path name
 * Increment list index numbers if within a list and ch is '}'
 *****************************************************************************/
static void path_remove(char *path, size_t *pathlen, unsigned long *idx)
{
    char *p;

    if (debug>1) fprintf(stderr, " - path_remove(%s, %ld) at %s:%ld\n",
                            path, *pathlen, current_file, linenum);

    if (!(p=find_last_slash(path)))
    {
        *path='\0';
        *pathlen=0;
    }
    else
    {
        /* If removing a list index number, record the number that is removed */
        if (idx)
        {
            if (gen_index)
            {
                char *q;
                unsigned long n;

                if (isdigit(*(p+1)) && (n=strtoul(p+1, &q, 10))!=ULONG_MAX &&
                        *q=='\0') *idx=n;   /* valid index number */
                else *idx=ULONG_MAX;        /* not a number */
            }
            else if (*(p+1)=='#' && *(p+2)=='\0') *idx=1;
        }

        *p='\0';
        *pathlen=strlen(path);
    }
}

/*****************************************************************************
 * str_append()
 *
 * Add new character to a string
 *
 * Returns >0 if an error occurred
 *****************************************************************************/
static int str_append(char ch, char *s, size_t *len)
{
    if (*len==MAXBUF-1)
    {
        fprintf(stderr, "%s: error reading '%s': string exceeded "
                            "%d bytes at line %ld\n", prog, current_file,
                                                            MAXBUF, linenum);
        return 1;
    }
    s[*len]=ch;
    s[*len+1]='\0';
    (*len)++;
    return 0;
}

/*****************************************************************************
 * is_slice()
 *
 * Returns >0 if given string is in the slices buffer (added by -s)
 *****************************************************************************/
static int is_slice(const char *s)
{
    int i;
    char *p;

    for (i=0, p=slices; i<num_slices; i++, p+=strlen(p)+1)
        if (!strcmp(s, p)) return 1;

    return 0;
}

/*****************************************************************************
 * process_json()
 *
 * Process a single JSON file, which may or may not be compressed
 *
 * Returns >0 if an error occurred
 *****************************************************************************/
static int process_json(const char *infile)
{
    FILE *outfp=NULL, *slicefp=NULL;
    gzFile gzin=NULL, gzout=NULL;
    char outfile[MAXBUF], slicefile[MAXBUF];
    int errnum;

    char name[MAXBUF]="", path[MAXBUF]="", outcompress;
    char in_quotes=0, in_value=0, last_comma=0, protect=0, in_unicode=0;
    int ch;
    long unicode=0;
    unsigned long idx=ULONG_MAX;
    size_t namelen=0, pathlen=0, valueout=0;
    struct stat stbuf;
    time_t in_mtime=0;

    if (debug)
    {
        fputs("------------------------------------------------------------------------------\n", stderr);
        fprintf(stderr, " - process_json %s\n",
                                from_stdin ? "<stdin>" : infile);
    }

    if (!from_stdin)
    {
        /* Open input file stream */
        if (stat(infile, &stbuf)==0) in_mtime=stbuf.st_mtime;
        else
        {
            fprintf(stderr, "%s: cannot open '%s' for reading: %s\n",
                                            prog, infile, strerror(errno));
            return 1;
        }
        if (!(gzin=gzopen(infile, "r")))
        {
            fprintf(stderr, "%s: cannot open '%s' for reading: %s\n",
                                            prog, infile, strerror(errno));
            return 1;
        }
        gzbuffer(gzin, GZ_BUFFER_SIZE);
    }

    /* Where are we sending the output? */
    if (to_stdout)
    {
        outcompress=0;
        outfp=stdout;
        if (debug) fputs(" - process_json writing to stdout\n", stderr);
    }
    else
    {
        size_t len;

        /* Compute output filename */
        len=strlen(infile);
        outcompress=(gzlevel ? 1: 0);
        if (!outdir)
        {
            if (len+len_suffix+(outcompress ? 3 : 0) > MAXBUF)
            {
                fprintf(stderr, "%s: filename too long: %s\n", prog, infile);
                return 1;
            }
            strcpy(outfile, infile);
        }
        else
        {
            char buf[MAXBUF], *p;

            if (len > MAXBUF)
            {
                fprintf(stderr, "%s: filename too long: %s\n", prog, infile);
                return 1;
            }
            strcpy(buf, infile);
            p=basename(buf);
            if (len_outdir+strlen(p)+len_suffix+(outcompress?3:0)+1 > MAXBUF)
            {
                fprintf(stderr, "%s: filename too long: %s\n", prog, infile);
                return 1;
            }
            strcpy(outfile, outdir);
            strcat(outfile, "/");
            strcat(outfile, p);
        }

        if (len>3 && !strcmp(outfile+len-3, ".gz"))
        { /* strip .gz suffix from output filename */
            outfile[len-3]='\0';
            len-=3;
        }

        if (len>len_strip && !strcmp(outfile+len-len_strip, strip))
        { /* strip .json suffix from output filename */
            outfile[len-len_strip]='\0';
            len-=len_strip;
        }

        strcat(outfile, suffix);
        if (outcompress) strcat(outfile, ".gz");

        /* If outfile exists and is newer, do nothing */
        if (stat(outfile, &stbuf)==0)
        {
            if (debug>1) fprintf(stderr, " - in_mtime=%ld out_mtime=%ld "
                                             "out_size=%ld\n", in_mtime,
                                                 stbuf.st_mtime, stbuf.st_size);
            if (!force && stbuf.st_size>0 && stbuf.st_mtime>in_mtime)
            {
                if (debug) fprintf(stderr, " - process_json ignoring file "
                                           "'%s' as '%s' exists and is newer\n",
                                               infile, outfile);
                return 0;
            }
        }
        else if (errno!=ENOENT)
        {
            fprintf(stderr, "%s: error testing output filename '%s': %s\n",
                                            prog, outfile, strerror(errno));
            return 1;
        }

        if (debug) fprintf(stderr, " - process_json writing to %s\n", outfile);

        /* Open output file stream */
        if (outcompress)
        {
            if (!(gzout=gzopen(outfile, gzmode)))
            {
                fprintf(stderr, "%s: cannot open '%s' for writing: %s\n",
                                                prog, outfile, strerror(errno));
                return 1;
            }
            gzbuffer(gzout, GZ_BUFFER_SIZE);
        }
        else
        {
            if (!(outfp=fopen(outfile, "w")))
            {
                fprintf(stderr, "%s: cannot open '%s' for writing: %s\n",
                                                prog, outfile, strerror(errno));
                return 1;
            }
        }

        /* Compute slice filename */
        if (num_slices>0)
        {
            len=strlen(outfile);
            if (len+len_slice > MAXBUF)
            {
                fprintf(stderr, "%s: filename too long: %s\n", prog, outfile);
                return 1;
            }

            strcpy(slicefile, outfile);
            if (len>3 && !strcmp(slicefile+len-3, ".gz"))
            { /* strip .gz suffix from slice filename */
                slicefile[len-3]='\0';
                len-=3;
            }
            strcat(slicefile, slice);
        }
    }

    linenum=1;
    current_file=infile;

    /* Process input file one character at a time */
    while (!terminate && (ch=(from_stdin ? getchar() : gzgetc(gzin)))!=-1)
    {
        if (!in_quotes)
        { /* outside quotes */
            switch (ch)
            {
                case ' ':
                case '\t':
                case '\r': /* ignore whitespace */
                    break;

                case '\n':
                    linenum++;
                    break;

                case '{': /* begin-object */
                    if (terminals && pathlen)
                    {
                        if (outcompress) gzprintf(gzout, "%s\n", path);
                        else fprintf(outfp, "%s\n", path);
                    }
                    in_value=0;
                    break;

                case '[': /* begin-array */
                    if (terminals && pathlen)
                    {
                        if (outcompress) gzprintf(gzout, "%s\n", path);
                        else fprintf(outfp, "%s\n", path);
                    }
                    in_value=2;
                    if (!last_comma)
                    {
                        if (gen_index && path_append("0", path, &pathlen))
                            return 1;
                        else if (!gen_index && path_append("#", path, &pathlen))
                            return 1;
                    }
                    break;

                case ':': /* end of path name component */
                    in_value=1;
                    if (path_append(name, path, &pathlen)) return 1;
                    *name='\0';
                    namelen=0;
                    break;

                case ',': /* next item */
                    if (valueout)
                    { /* end of value */
                        if (to_stdout) putchar('\n');
                        else if (outcompress) gzputc(gzout, '\n');
                        else putc('\n', outfp);

                        stop_slice();
                        valueout=0;
                    }

                    if (in_value==2) /* element value */
                        path_remove(path, &pathlen, &idx);

                    if (in_value==1) path_remove(path, &pathlen, NULL);
                    else if (in_value!=1 && idx<ULONG_MAX)
                    { /* increment last index number */
                        if (gen_index)
                        {
                            char buf[30];

                            idx++;
                            snprintf(buf, 30, "%lu", idx);
                            if (path_append(buf, path, &pathlen)) return 1;
                        }
                        else if (path_append("#", path, &pathlen)) return 1;
                    }

                    if (in_value<2) in_value=0;
                    idx=ULONG_MAX;
                    *name='\0';
                    namelen=0;
                    break;

                case '}': /* end-object */
                case ']': /* end-array */
                    if (valueout)
                    { /* end of value */
                        if (to_stdout) putchar('\n');
                        else if (outcompress) gzputc(gzout, '\n');
                        else putc('\n', outfp);

                        stop_slice();
                        valueout=0;
                    }

                    /* remove last path component */
                    if (in_value) path_remove(path, &pathlen, NULL);
                    path_remove(path, &pathlen, ch==']' ? NULL : &idx);

                    if (ch==']') idx=ULONG_MAX;
                    in_value=0;
                    *name='\0';
                    namelen=0;
                    break;

                case '"':
                    /* open quotes */
                    in_quotes=1;
                    if (in_value) putvalue(ch);
                    break;

                default:
                    if (in_value) putvalue(ch);
                    else str_append(ch, name, &namelen);
                    break;
            }
            if (ch==',') last_comma=1;
            else last_comma=0;
        }
        else
        { /* inside quotes */
            if (!protect && ch=='"')
            { /* close quotes */
                in_quotes=0;
                if (in_value) putvalue(ch);
            }
            else if (protect && ch=='u')
            { /* start of unicode sequence \uXXXX */
                in_unicode=1;
                unicode=0;
                protect=0;
            }
            else if (in_unicode)
            { /* encode to UTF-8 */
                unicode<<=4;
                if (ch>='A' && ch<='F') ch=tolower(ch);
                if (ch>='a' && ch<='f') unicode|=(ch-'a'+10);
                else if (ch>='0' && ch<='9') unicode|=(ch-'0');
                else goto unicode_error;

                if (in_unicode++==4)
                {
                    char *target, value[4];
                    size_t *targetlen, valuelen=0;

                    if (in_value)
                    { /* updating value */
                        target=value;
                        targetlen=&valuelen;
                    }
                    else
                    { /* updating pathname component */
                        target=name;
                        targetlen=&namelen;
                    }

                    /* convert to UTF-8 as per: http://stackoverflow.com/questions/4607413/c-library-to-convert-unicode-code-points-to-utf8 */
                    if (unicode<0x80)
                        str_append(unicode, target, targetlen);
                    else if (unicode<0x800)
                    {
                        str_append(192+unicode/64, target, targetlen);
                        str_append(128+unicode%64, target, targetlen);
                    }
                    else if (unicode-0xd800u<0x800) goto unicode_error;
                    else if (unicode<0x10000)
                    {
                        str_append(224+unicode/4096, target, targetlen);
                        str_append(128+unicode/4096%64, target, targetlen);
                        str_append(128+unicode/64%64, target, targetlen);
                        str_append(128+unicode%64, target, targetlen);
                    }
                    else if (unicode<0x110000)
                    {
                        str_append(240+unicode/262144, target, targetlen);
                        str_append(128+unicode/4096%64, target, targetlen);
                        str_append(128+unicode/64%64, target, targetlen);
                        str_append(128+unicode%64, target, targetlen);
                    }
                    else goto unicode_error;

                    in_unicode=0;

                    if (in_value)
                    {
                        if (outcompress) gzputs(gzout, value);
                        else fputs(value, outfp);
                    }
                }
            }
            else
            { /* any other character inside quotes */
                if (!protect && ch=='\\') protect=1;
                else
                {
                    if (protect)
                    {
                        if (multiline && ch=='n') ch='\n';
                        else
                        {
                            if (in_value) putvalue('\\');
                            else str_append('\\', name, &namelen);
                        }
                    }
                    protect=0;

                    if (in_value)
                    {
                        if (multiprefix && ch=='\n')
                        {
                            if (outcompress)
                                gzprintf(gzout, "\"\n%s .= \"", path);
                            else fprintf(outfp, "\"\n%s .= \"", path);
                        }
                        else putvalue(ch);
                    }
                    else str_append(ch, name, &namelen);
                }
            }
        }
    }

    if (!from_stdin)
    {
        /* Close input file stream */
        errnum=gzclose(gzin);
        if (errnum!=Z_OK)
        {
            fprintf(stderr, "%s: error closing '%s': %s\n", prog, infile,
                                                        gz_strerror(errnum));
            return 1;
        }
    }
    else if (ferror(stdin))
    {
        fprintf(stderr, "%s: error reading from stdin: %s\n",
                                            prog, strerror(errno));
        return 1;
    }

    /* Close output file stream */
    if (outcompress)
    {
        errnum=gzclose(gzout);
        if (errnum!=Z_OK)
        {
            fprintf(stderr, "%s: error closing '%s': %s\n", prog, outfile,
                                                        gz_strerror(errnum));
            return 1;
        }
    }
    else
    {
        if (outfp!=stdout)
        {
            if (fclose(outfp))
            {
                fprintf(stderr, "%s: error closing '%s': %s\n",
                                            prog, outfile, strerror(errno));
                return 1;
            }
        }
        else if (ferror(stdout))
        {
            fprintf(stderr, "%s: error writing to stdout: %s\n",
                                                prog, strerror(errno));
            return 1;
        }
    }

    /*
     * If terminating due to receipt of signal, delete output file
     * as it may be incomplete
     */
    if (terminate && !to_stdout) unlink(outfile);
    return 0;

unicode_error:
    fprintf(stderr, "%s: error reading '%s': string unicode error "
                        "at line %ld\n", prog, current_file, linenum);
    return 1;
}

/*****************************************************************************
 * Main entry-point
 *****************************************************************************/
int main(int argc, char **argv)
{
    int ch, errflg=0;

    prog=basename(argv[0]);

    /* Parse command-line arguments */
    while ((ch=getopt(argc, argv, "123456789CDd:eFfhInO:pR:Ss:t")) != -1)
    {
        switch (ch)
        {
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                gzlevel=ch-'0';
                break;

            case 'C': gzlevel=6; break;
            case 'D': debug++; break;
            case 'd': outdir=optarg; break;
            case 'e': unescape=1; break;
            case 'F': force=1; break;
            case 'f': filelist=1; break;
            case 'h': terminals=0; break;
            case 'I': gen_index=0; break;
            case 'n': multiline=1; break;
            case 'O': suffix=optarg; break;
            case 'p': multiprefix=1; break;
            case 'R': strip=optarg; break;
            case 'S': slice=optarg; break;
            case 's':
                if (!slices)
                {
                    slices=strdup(optarg);
                    num_slices=1;
                    size_slices=strlen(optarg)+1;
                }
                else
                {
                    size_t newlen;

                    newlen=size_slices+strlen(optarg)+1;
                    if (!(slices=realloc(slices, newlen)))
                    {
                        fprintf(stderr, "%s: error resizing slices buffer: "
                                                "%s\n", prog, strerror(errno));
                        return 1;
                    }
                    strcpy(slices+size_slices, optarg);
                    size_slices=newlen;
                    num_slices++;
                }
            case 't': to_stdout=1; break;
            case '?':
                fprintf(stderr, "%s: unknown option: -%c\n", prog, ch);
                errflg=1;
                break;
        }
    }

    if (errflg || optind==argc)
    {
        fprintf(stderr, "Syntax: %s [-19CDeFfhInpt] [-d <outdir>] [-O <suffix>]"
                        " \\\n\t\t[-R <suffix>] [-S <suffix>] [-s <slice>] "
                                                "<file> [<file> ...]\n", prog);
        return 1;
    }

    if (outdir) len_outdir=strlen(outdir);
    len_suffix=strlen(suffix);
    len_strip=strlen(strip);
    len_slice=strlen(slice);
    if (gzlevel) gzmode[2]='0'+gzlevel;

    /* Set-up signal handler before starting to read any JSON profiles */
    signal(SIGINT, sigterm);
    signal(SIGTERM, sigterm);

    if (filelist)
    { /* process list of files containing filenames */
        to_stdout=0;

        for (; optind<argc; optind++)
        {
            FILE *fp;
            char buf[MAXBUF];
            size_t len;

            if (argv[optind][0]=='-' && argv[optind][1]=='\0')
                fp=stdin; /* filename "-" reads file list from stdin */
            else if (!(fp=fopen(argv[optind], "r")))
            {
                fprintf(stderr, "%s: cannot open '%s' for reading: %s\n",
                                        prog, argv[optind], strerror(errno));
                return 1;
            }

            while (!terminate && fgets(buf, MAXBUF, fp))
            {
                len=strlen(buf);
                if (len)
                {
                    if (*buf=='#') continue;   /* ignore comments */
                    if (buf[len-1]=='\n') buf[len-1]='\0';
                    if (process_json(buf)) return 1;
                }
            }

            if (fp!=stdin)
            {
                if (fclose(fp))
                {
                    fprintf(stderr, "%s: error closing '%s': %s\n",
                                        prog, argv[optind], strerror(errno));
                    return 1;
                }
            }
            else if (ferror(stdin))
            {
                fprintf(stderr, "%s: error reading from stdin: %s\n",
                                                prog, strerror(errno));
                return 1;
            }
        }
    }
    else
    { /* process list of input files provided as command-line arguments */
        if (optind==argc-1 && !num_slices)
        { /* if single filename "-" read from stdin and write to stdout */
            if (argv[optind][0]=='-' && argv[optind][1]=='\0')
                from_stdin=to_stdout=1;
        }
        else to_stdout=0;

        for (; !terminate && optind<argc; optind++)
            if (process_json(argv[optind])) return 1;
    }
    return 0;
}
