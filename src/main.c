/*  packMP2 — MPEG Audio Layer II lossless transform + compression
    Unified CLI with switches for testing. v0.4
    Copyright (C) 2009-2010 Michael Henke, 2026 Tovy. GPLv3.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>

#ifdef _WIN32
# include <fcntl.h>
# include <io.h>
# define SET_BINARY_MODE(f) _setmode(_fileno(f), _O_BINARY)
#else
# define SET_BINARY_MODE(f) ((void)0)
#endif

#include "zpaq_c.h"

#define VERSION "0.5"

/* Forward declarations */
extern int unpack(FILE *in, FILE *out);
extern int unpack_optimized(FILE *in, FILE *out);
extern int pack(FILE *in, FILE *out);
extern int pack_optimized(FILE *in, FILE *out);
extern int tcam2_compress(FILE *in, FILE *out, int level);
extern int tcam2_compress_dict(FILE *in, FILE *out, int level,
                                const unsigned char *dict, size_t dict_size);
extern int tcam2_decompress(FILE *in, FILE *out);
extern int tcam2_decompress_dict(FILE *in, FILE *out,
                                  const unsigned char *dict, size_t dict_size);
extern int tcam2_quiet;

static void print_help(void) {
    fprintf(stderr,
        "packMP2 v" VERSION " — MPEG Audio Layer II lossless transform + compression\n"
        "Sub-project of packMP3. https://github.com/YadeWira/packMP2\n"
        "\n"
        "  packmp2 <command> [options]\n"
        "\n"
        "Commands:\n"
        "  u, unpack      mp2 -> um2\n"
        "  p, pack        um2 -> mp2\n"
        "  c, compress    um2 -> tcam2\n"
        "  d, decompress  tcam2 -> um2\n"
        "  x, pipe        mp2 -> um2 -> tcam2 -> um2 -> mp2\n"
        "\n"
        "Options:\n"
        "  -i, --input F   Read from file (default: stdin)\n"
        "  -o, --output F  Write to file (default: stdout)\n"
        "  -q, --quiet     Suppress progress messages\n"
        "  -l, --level N   zstd level 1-9 (default: 1)\n"
        "  -O, --optimized SCFSI packing + scalefactor delta (better ratio)\n"
        "  --zpaq N        Use zpaq context-mixing (1-5, best ratio)\n"
        "  --raw           c/d passthrough, no compression (testing)\n"
        "  -b, --benchmark Report timing + ratio\n"
        "  -s, --stats     Show detailed statistics\n"
        "  --compare       Run with and without dict, compare sizes\n"
        "  --no-dict       Compress without dictionary\n"
        "  --dict FILE     Use external dictionary file\n"
        "  --list          Show file metadata without processing\n"
        "  --test-all DIR  Batch test all .mp2 files in directory\n"
        "  --csv           Output in CSV format\n"
        "  --verify        After pipe, verify byte-exact roundtrip\n"
        "  -V, --version   Print version\n"
        "  -h, --help      This help\n");
}

/* --list: show um2 or tcam2 metadata */
static int list_file(const char *fn) {
    FILE *f = fopen(fn, "rb");
    if (!f) { perror(fn); return 1; }
    unsigned char hdr[9];
    if (fread(hdr, 1, 9, f) != 9) { fclose(f); return 1; }
    if (hdr[0]=='u' && hdr[1]=='m' && hdr[2]=='2') {
        unsigned long pre = (hdr[4]<<24)|(hdr[5]<<16)|(hdr[6]<<8)|hdr[7];
        fprintf(stderr, "um2 v%u  preamble=%lu  ", hdr[3], pre);
        /* Count blocks */
        fseek(f, 8+pre, SEEK_SET);
        int blocks=0, total_frames=0;
        while (1) {
            int hi=getc(f), lo=getc(f);
            if (hi==EOF) break;
            int nf = (hi<<8)|lo;
            if (nf==0) { total_frames=-1; break; }
            blocks++; total_frames+=nf;
            /* Skip block data — too complex to parse fully.
               Just report block count. */
            break; /* only count first block for speed */
        }
        long fsize; fseek(f,0,SEEK_END); fsize=ftell(f);
        fprintf(stderr, "blocks>=%d  file=%ld bytes\n", blocks, fsize);
    } else if (hdr[0]=='T' && hdr[1]=='C' && hdr[2]=='A' && hdr[3]=='M') {
        long osz = (hdr[5]<<24)|(hdr[6]<<16)|(hdr[7]<<8)|hdr[8];
        long fsize; fseek(f,0,SEEK_END); fsize=ftell(f);
        fprintf(stderr, "tcam2 v%u  original=%ld  compressed=%ld  ratio=%.1f%%\n",
                hdr[4], osz, fsize-9, 100.0*(fsize-9)/osz);
    } else {
        fprintf(stderr, "unknown format: %02x%02x%02x%02x\n", hdr[0],hdr[1],hdr[2],hdr[3]);
    }
    fclose(f);
    return 0;
}

/* Load external dictionary from file */
static unsigned char *load_dict(const char *fn, size_t *dsize) {
    FILE *f = fopen(fn, "rb");
    if (!f) { perror(fn); return NULL; }
    fseek(f, 0, SEEK_END);
    *dsize = ftell(f);
    rewind(f);
    unsigned char *d = malloc(*dsize);
    if (!d) { fclose(f); return NULL; }
    fread(d, 1, *dsize, f);
    fclose(f);
    return d;
}

/* Test all MP2 files in a directory (POSIX only) */
static int test_all(const char *dir, int csv) {
#ifndef _WIN32
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return 1; }
    if (!csv) fprintf(stderr, "Testing MP2 files in %s...\n", dir);
    if (csv) printf("file,orig_size,tcam2_size,ratio_%%,roundtrip\n");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char *n = e->d_name;
        int nl = strlen(n);
        if (nl<4) continue;
        char *ext = n+nl-4;
        if (strcasecmp(ext,".mp2")!=0 && strcasecmp(ext,".MP2")!=0) continue;
        char path[1024]; snprintf(path,sizeof(path),"%s/%s",dir,n);
        FILE *in = fopen(path, "rb");
        if (!in) continue;
        /* Read input */
        fseek(in,0,SEEK_END); long orig=ftell(in); rewind(in);
        unsigned char *idata = malloc(orig);
        fread(idata,1,orig,in); fclose(in);
        /* Pipe through TCAM2 */
        FILE *mp2_f=tmpfile();
        fwrite(idata,1,orig,mp2_f); rewind(mp2_f);
        FILE *um2_f=tmpfile(), *tc_f=tmpfile(), *um2_r=tmpfile();
        int ok=0; long t2sz=0;
        if (!unpack(mp2_f,um2_f)) {
            rewind(um2_f);
            if (!tcam2_compress(um2_f,tc_f,1)) {
                t2sz=ftell(tc_f);
                rewind(tc_f);
                if (!tcam2_decompress(tc_f,um2_r)) {
                    rewind(um2_r);
                    FILE *out_f=tmpfile();
                    if (!pack(um2_r,out_f)) {
                        long osz=ftell(out_f);
                        if (osz==orig) {
                            rewind(out_f);
                            unsigned char *od=malloc(osz);
                            fread(od,1,osz,out_f);
                            ok=(memcmp(idata,od,orig)==0);
                            free(od);
                        }
                        fclose(out_f);
                    }
                }
            }
        }
        fclose(mp2_f);fclose(um2_f);fclose(tc_f);fclose(um2_r);
        free(idata);
        if (csv) printf("%s,%ld,%ld,%.1f,%s\n",n,orig,t2sz,100.0*t2sz/orig,ok?"OK":"FAIL");
        else fprintf(stderr,"  %-30s %8ld -> %8ld (%5.1f%%) %s\n",n,orig,t2sz,100.0*t2sz/orig,ok?"OK":"FAIL");
    }
    closedir(d);
#else
    fprintf(stderr, "packMP2: --test-all not available on Windows\n");
    (void)dir; (void)csv;
#endif
    return 0;
}

int main(int argc, char **argv) {
    SET_BINARY_MODE(stdin); SET_BINARY_MODE(stdout);
    if (argc < 2) { print_help(); return 1; }

    /* --list, --test-all, -V, -h don't need a command */
    if (strcmp(argv[1],"--list")==0 && argc>=3) { return list_file(argv[2]); }
    if (strcmp(argv[1],"--test-all")==0 && argc>=3) {
        int csv=0; for(int i=1;i<argc;i++) if(strcmp(argv[i],"--csv")==0) csv=1;
        return test_all(argv[2], csv);
    }
    if (strcmp(argv[1],"-V")==0||strcmp(argv[1],"--version")==0)
        { fprintf(stderr,"packMP2 v" VERSION "\n"); return 0; }
    if (strcmp(argv[1],"-h")==0||strcmp(argv[1],"--help")==0)
        { print_help(); return 0; }

    /* Parse command */
    char cmd=0; int arg_start=1;
    if (strcmp(argv[1],"u")==0||strcmp(argv[1],"unpack")==0) cmd='u';
    else if (strcmp(argv[1],"p")==0||strcmp(argv[1],"pack")==0) cmd='p';
    else if (strcmp(argv[1],"c")==0||strcmp(argv[1],"compress")==0) cmd='c';
    else if (strcmp(argv[1],"d")==0||strcmp(argv[1],"decompress")==0) cmd='d';
    else if (strcmp(argv[1],"x")==0||strcmp(argv[1],"pipe")==0) cmd='x';
    else { print_help(); return 1; }

    /* Parse options */
    char *in_file=NULL, *out_file=NULL, *dict_file=NULL;
    int quiet=0, benchmark=0, verify=0, raw=0, level=1, stats=0, no_dict=0, compare=0, csv=0, optimized=0, zpaq_level=0;
    char *zpaq_method=NULL;
    unsigned char *ext_dict=NULL; size_t ext_dict_size=0;

    for (int i=arg_start+1; i<argc; i++) {
        if (strcmp(argv[i],"-q")==0||strcmp(argv[i],"--quiet")==0) quiet=1;
        else if (strcmp(argv[i],"-b")==0||strcmp(argv[i],"--benchmark")==0) benchmark=1;
        else if (strcmp(argv[i],"-s")==0||strcmp(argv[i],"--stats")==0) stats=1;
        else if (strcmp(argv[i],"--verify")==0) verify=1;
        else if (strcmp(argv[i],"--raw")==0) raw=1;
        else if (strcmp(argv[i],"-O")==0||strcmp(argv[i],"--optimized")==0) optimized=1;
        else if (strcmp(argv[i],"--zpaq")==0){
            if(i+1>=argc){fprintf(stderr,"packMP2: missing level for --zpaq\n");return 1;}
            zpaq_level=atoi(argv[++i]); if(zpaq_level<1)zpaq_level=1; if(zpaq_level>5)zpaq_level=5;}
        else if (strcmp(argv[i],"--zpaq-method")==0){
            if(i+1>=argc){fprintf(stderr,"packMP2: missing method for --zpaq-method\n");return 1;}
            zpaq_method=argv[++i]; zpaq_level=-1; /* -1 = raw method mode */}
        else if (strcmp(argv[i],"--no-dict")==0) no_dict=1;
        else if (strcmp(argv[i],"--compare")==0) compare=1;
        else if (strcmp(argv[i],"--csv")==0) csv=1;
        else if (strcmp(argv[i],"-V")==0||strcmp(argv[i],"--version")==0)
            { fprintf(stderr,"packMP2 v" VERSION "\n"); return 0; }
        else if (strcmp(argv[i],"-h")==0||strcmp(argv[i],"--help")==0)
            { print_help(); return 0; }
        else if (strcmp(argv[i],"-i")==0||strcmp(argv[i],"--input")==0){
            if(i+1>=argc){fprintf(stderr,"packMP2: missing argument for %s\n",argv[i]);return 1;}
            in_file=argv[++i];}
        else if (strcmp(argv[i],"-o")==0||strcmp(argv[i],"--output")==0){
            if(i+1>=argc){fprintf(stderr,"packMP2: missing argument for %s\n",argv[i]);return 1;}
            out_file=argv[++i];}
        else if (strcmp(argv[i],"-l")==0||strcmp(argv[i],"--level")==0){
            if(i+1>=argc){fprintf(stderr,"packMP2: missing argument for %s\n",argv[i]);return 1;}
            level=atoi(argv[++i]);}
        else if (strcmp(argv[i],"--dict")==0){
            if(i+1>=argc){fprintf(stderr,"packMP2: missing argument for %s\n",argv[i]);return 1;}
            dict_file=argv[++i];}
        else { fprintf(stderr,"packMP2: unknown option: %s\n",argv[i]); return 1; }
    }

    tcam2_quiet = quiet;

    /* Load external dict if specified */
    if (dict_file) {
        ext_dict = load_dict(dict_file, &ext_dict_size);
        if (!ext_dict) return 1;
    }

    /* Open files */
    FILE *in=stdin, *out=stdout;
    if (in_file) { in=fopen(in_file,"rb"); if(!in){perror(in_file);return 1;} }
    if (out_file) { out=fopen(out_file,"wb"); if(!out){perror(out_file);return 1;} }

    clock_t t0=clock();
    int rc=0, cc;

    /* --compare: compress with and without dict, compare sizes */
    if (compare && cmd=='c') {
        if (!quiet) fprintf(stderr,"packMP2: comparing dict vs no-dict...\n");
        /* Read input */
        long isz=0,icap=65536;unsigned char*idata=malloc(icap);
        int cc; while((cc=getc(in))!=EOF){if(isz>=icap){icap*=2;idata=realloc(idata,icap);}idata[isz++]=cc;}
        /* Compress with dict */
        FILE *f1=tmpfile(); FILE *in1=tmpfile();
        fwrite(idata,1,isz,in1); rewind(in1);
        tcam2_compress(in1,f1,level);
        long sz1=ftell(f1);
        /* Compress without dict */
        FILE *f2=tmpfile(); FILE *in2=tmpfile();
        fwrite(idata,1,isz,in2); rewind(in2);
        tcam2_compress_dict(in2,f2,level,NULL,0);
        long sz2=ftell(f2);
        /* Write dict version to output */
        rewind(f1);
        while((cc=getc(f1))!=EOF) putc(cc,out);
        fclose(f1);fclose(f2);fclose(in1);fclose(in2);
        if (csv) printf("%ld,%ld,%.1f\n",sz1,sz2,100.0*(sz2-sz1)/sz2);
        else if (!quiet) fprintf(stderr,"  with dict: %ld  without: %ld  dict saves: %.1f%%\n",
                                  sz1,sz2,100.0*(sz2-sz1)/sz2);
        free(idata);
    } else {
        /* Normal command execution */
        switch (cmd) {
        case 'u': if(!quiet)fprintf(stderr,"packMP2: unpacking%s...\n",optimized?" (optimized)":"");
                  rc=optimized?unpack_optimized(in,out):unpack(in,out); break;
        case 'p': if(!quiet)fprintf(stderr,"packMP2: packing%s...\n",optimized?" (optimized)":"");
                  rc=optimized?pack_optimized(in,out):pack(in,out); break;
        case 'c':
            if(raw){if(!quiet)fprintf(stderr,"packMP2: raw copy...\n");while((cc=getc(in))!=EOF)putc(cc,out);}
            else if(zpaq_level>0 || zpaq_method){
                if(!quiet){
                  if(zpaq_method) fprintf(stderr,"packMP2: compressing (zpaq method %s)...\n",zpaq_method);
                  else fprintf(stderr,"packMP2: compressing (zpaq level %d)...\n",zpaq_level);
                }
                long isz=0,icap=65536;unsigned char*idata=malloc(icap);
                while((cc=getc(in))!=EOF){if(isz>=icap){icap*=2;idata=realloc(idata,icap);}idata[isz++]=cc;}
                unsigned char*zout=NULL;size_t zsz=0;
                if(zpaq_method) rc=zpaq_compress_method(idata,isz,&zout,&zsz,zpaq_method);
                else rc=zpaq_compress(idata,isz,&zout,&zsz,zpaq_level);
                if(rc==0){fwrite(zout,1,zsz,out);free(zout);}
                else fprintf(stderr,"packMP2: zpaq compression failed\n");
                free(idata);
            }
            else{
                if(!quiet)fprintf(stderr,"packMP2: compressing (level %d)...\n",level);
                if(no_dict) rc=tcam2_compress_dict(in,out,level,NULL,0);
                else if(ext_dict) rc=tcam2_compress_dict(in,out,level,ext_dict,ext_dict_size);
                else rc=tcam2_compress(in,out,level);
            } break;
        case 'd':
            if(raw){if(!quiet)fprintf(stderr,"packMP2: raw copy...\n");while((cc=getc(in))!=EOF)putc(cc,out);}
            else{
                /* Auto-detect: peek first byte for zpaq vs tcam2 */
                int first=getc(in);
                if(first==EOF){fprintf(stderr,"packMP2: empty input\n");rc=1;}
                else if(first==0x37||zpaq_level>0||zpaq_method){  /* '7' = zpaq magic */
                    if(!quiet)fprintf(stderr,"packMP2: decompressing (zpaq)...\n");
                    long isz=0,icap=65536;unsigned char*idata=malloc(icap);
                    idata[isz++]=(unsigned char)first;
                    while((cc=getc(in))!=EOF){if(isz>=icap){icap*=2;idata=realloc(idata,icap);}idata[isz++]=cc;}
                    unsigned char*zout=NULL;size_t zsz=0;
                    rc=zpaq_decompress(idata,isz,&zout,&zsz);
                    if(rc==0){fwrite(zout,1,zsz,out);free(zout);}
                    else fprintf(stderr,"packMP2: zpaq decompression failed\n");
                    free(idata);
                } else {
                    ungetc(first,in);
                    if(!quiet)fprintf(stderr,"packMP2: decompressing...\n");
                    if(no_dict) rc=tcam2_decompress_dict(in,out,NULL,0);
                    else if(ext_dict) rc=tcam2_decompress_dict(in,out,ext_dict,ext_dict_size);
                    else rc=tcam2_decompress(in,out);
                }
            } break;
        case 'x': {
            if(!quiet)fprintf(stderr,"packMP2: pipeline...\n");
            long isz=0,icap=65536;unsigned char*idata=malloc(icap);
            while((cc=getc(in))!=EOF){if(isz>=icap){icap*=2;idata=realloc(idata,icap);}idata[isz++]=cc;}
            FILE *mp2_f=tmpfile();fwrite(idata,1,isz,mp2_f);rewind(mp2_f);
            FILE *um2_f=tmpfile(),*tc_f=tmpfile(),*um2_r=tmpfile();
            if(!(rc=optimized?unpack_optimized(mp2_f,um2_f):unpack(mp2_f,um2_f))){rewind(um2_f);
            if(zpaq_level>0 || zpaq_method){
                /* zpaq path: read um2, compress with zpaq, decompress */
                long um2sz=0;unsigned char*um2data=NULL;
                {long cap=65536;um2data=malloc(cap);
                 while((cc=getc(um2_f))!=EOF){if(um2sz>=cap){cap*=2;um2data=realloc(um2data,cap);}um2data[um2sz++]=cc;}}
                unsigned char*zout=NULL;size_t zsz=0;
                if(zpaq_method) rc=zpaq_compress_method(um2data,um2sz,&zout,&zsz,zpaq_method);
                else rc=zpaq_compress(um2data,um2sz,&zout,&zsz,zpaq_level);
                if(!rc){
                    unsigned char*dec=NULL;size_t dsz=0;
                    if(!(rc=zpaq_decompress(zout,zsz,&dec,&dsz))){
                        fwrite(dec,1,dsz,um2_r);rewind(um2_r);
                        rc=optimized?pack_optimized(um2_r,out):pack(um2_r,out);
                        free(dec);
                    }
                    free(zout);
                }
                free(um2data);
            } else {
            if(!(rc=tcam2_compress(um2_f,tc_f,level))){rewind(tc_f);
            if(!(rc=tcam2_decompress(tc_f,um2_r))){rewind(um2_r);
            rc=optimized?pack_optimized(um2_r,out):pack(um2_r,out);}}}
            }
            /* Verify */
            if(verify&&rc==0&&out_file){fclose(out);out=NULL;
                FILE *vrf=fopen(out_file,"rb");if(vrf){fseek(vrf,0,SEEK_END);
                long osz=ftell(vrf);rewind(vrf);unsigned char*od=malloc(osz);fread(od,1,osz,vrf);fclose(vrf);
                if(osz==isz&&memcmp(idata,od,isz)==0)fprintf(stderr,"verify: BYTE-EXACT OK\n");
                else{fprintf(stderr,"verify: MISMATCH\n");rc=1;}free(od);}}
            /* Stats */
            if(stats&&rc==0){rewind(um2_f);fseek(um2_f,0,SEEK_END);
                long um2sz=ftell(um2_f);rewind(tc_f);fseek(tc_f,0,SEEK_END);
                long tcsz=ftell(tc_f);
                if(csv) printf("%ld,%ld,%ld,%ld,%.1f,%.1f,%.1f\n",
                    isz,um2sz,tcsz,(long)ftell(out),100.0*um2sz/isz,100.0*tcsz/um2sz,100.0*ftell(out)/isz);
                else fprintf(stderr,"  mp2:%ld um2:%ld(%.1f%%) tcam2:%ld(%.1f%%) mp2_out:%ld(%.1f%%)\n",
                    isz,um2sz,100.0*um2sz/isz,tcsz,100.0*tcsz/um2sz,(long)ftell(out),100.0*ftell(out)/isz);}
            fclose(mp2_f);fclose(um2_f);fclose(tc_f);fclose(um2_r);free(idata);
        } break;
        default: print_help(); rc=1;
        }
    }

    double elapsed=(double)(clock()-t0)/CLOCKS_PER_SEC;
    if (benchmark && rc==0) {
        long outsz=ftell(out);
        if (csv) printf("%.3f,%ld\n",elapsed,outsz);
        else fprintf(stderr,"  time: %.3fs  output: %ld bytes\n",elapsed,outsz);
    }
    if (stats && rc==0 && cmd!='x') {
        long outsz=ftell(out);
        if (csv) printf("%.3f,%ld\n",elapsed,outsz);
        else fprintf(stderr,"  time=%.3fs  output=%ld bytes\n",elapsed,outsz);
    }

    if (in_file) fclose(in);
    if (out_file && out) fclose(out);
    free(ext_dict);
    return rc;
}
