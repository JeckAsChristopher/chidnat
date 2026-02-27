// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "common.h"
#include "error.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "func.h"
#include "compiler.h"
#include "vm.h"
#include "gc.h"
#include "bytecode.h"


#define MAX_IMPORTED 128
static char imported_files[MAX_IMPORTED][1024];
static int  imported_count = 0;
static bool already_imported(const char *p){for(int i=0;i<imported_count;i++) if(!strcmp(imported_files[i],p)) return true; return false;}
static void mark_imported(const char *p){if(imported_count<MAX_IMPORTED) strncpy(imported_files[imported_count++],p,1023);}


static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f){fprintf(stderr,"\033[1;31merror:\033[0m cannot open '\033[1;36m%s\033[0m'\n\n",path);return NULL;}
    fseek(f,0,SEEK_END);long sz=ftell(f);rewind(f);
    char *buf=(char*)malloc(sz+1);
    if(!buf){fclose(f);return NULL;}
    size_t n=fread(buf,1,sz,f);buf[n]='\0';fclose(f);return buf;
}
static bool file_exists(const char *p){struct stat st;return stat(p,&st)==0;}


static char chn_bin_dir[1024] = "";


static bool try_import_from(const char *dir, const char *name,
                             Compiler *parent_C);  

static bool search_chn_libs(const char *rel_path, Compiler *parent_C) {
    
    const char *slash = strrchr(rel_path, '/');
    const char *name  = slash ? slash + 1 : rel_path;
    
    char bare[256];
    strncpy(bare, name, sizeof(bare)-1); bare[sizeof(bare)-1] = '\0';
    char *dot = strrchr(bare, '.'); if (dot) *dot = '\0';

    
    char local_dir[1024] = "";
    if (parent_C->source_file[0]) {
        const char *sl = strrchr(parent_C->source_file, '/');
        if (sl) { size_t dl = (size_t)(sl - parent_C->source_file)+1;
                  memcpy(local_dir, parent_C->source_file, dl);
                  local_dir[dl] = '\0'; }
        else strncpy(local_dir, "./", sizeof(local_dir)-1);
    } else strncpy(local_dir, "./", sizeof(local_dir)-1);

    if (try_import_from(local_dir, bare, parent_C)) return true;

    
    char walk[1024];
    strncpy(walk, local_dir, sizeof(walk)-1);
    for (int depth = 0; depth < 16; depth++) {
        char libdir[1100];
        snprintf(libdir, sizeof(libdir), "%schn-libs/", walk);
        if (file_exists(libdir) && try_import_from(libdir, bare, parent_C))
            return true;
        
        int len = (int)strlen(walk);
        if (len <= 1) break;                      
        if (walk[len-1] == '/') walk[len-1] = '\0';
        char *up = strrchr(walk, '/');
        if (!up) break;
        *(up+1) = '\0';
        if (strcmp(walk, local_dir) == 0) break;  
    }

    
    if (chn_bin_dir[0]) {
        char instdir[1100];
        snprintf(instdir, sizeof(instdir), "%s/chn-libs/", chn_bin_dir);
        if (try_import_from(instdir, bare, parent_C)) return true;
    }

    return false;
}

static void path_dir(const char *path,char *out,size_t n){
    const char *sl=strrchr(path,'/');
    if(sl&&(size_t)(sl-path+2)<n){size_t dl=(size_t)(sl-path)+1;memcpy(out,path,dl);out[dl]='\0';}
    else out[0]='\0';
}
static void resolve_path(const char *from,const char *rel,char *out,size_t n){
    char dir[1024];path_dir(from,dir,sizeof(dir));
    if(dir[0]&&rel[0]!='/') snprintf(out,n,"%s%s",dir,rel);
    else{strncpy(out,rel,n-1);out[n-1]='\0';}
}
static void strip_ext(const char *path,char *out,size_t n){
    strncpy(out,path,n-1);out[n-1]='\0';
    char *dot=strrchr(out,'.');const char *sl=strrchr(out,'/');
    if(dot&&(!sl||dot>sl))*dot='\0';
}


bool (*chn_import_handler)(const char *path, Compiler *C)=NULL;


static bool compile_and_import(const char *src, Compiler *parent_C);


static bool try_import_from(const char *dir, const char *name, Compiler *parent_C){
    char base[1024];
    snprintf(base,sizeof(base),"%s%s",dir,name);
    if(already_imported(base)) return true;

    char fn_path[1100],chnc_path[1100],src_path[1100];
    snprintf(fn_path,  sizeof(fn_path),  "%s.function",base);
    snprintf(chnc_path,sizeof(chnc_path),"%s.chnc",    base);
    snprintf(src_path, sizeof(src_path), "%s.chn",     base);

    if(file_exists(fn_path)){
        if(already_imported(fn_path)) return true;
        mark_imported(fn_path); mark_imported(base);
        FunctionObject **fns;int nf;
        BCResult r=bc_read_functions(fn_path,&fns,&nf);
        if(r!=BC_OK){fprintf(stderr,"\033[1;31merror:\033[0m load '%s': %s\n\n",fn_path,bc_result_str(r));return false;}
        for(int i=0;i<nf;i++) if(parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i];
        free(fns);return true;
    }
    if(file_exists(chnc_path)){
        if(already_imported(chnc_path)) return true;
        mark_imported(chnc_path); mark_imported(base);
        Chunk dummy;FunctionObject **fns;int nf;
        BCResult r=bc_read_program(chnc_path,&dummy,&fns,&nf);
        if(r!=BC_OK){fprintf(stderr,"\033[1;31merror:\033[0m load '%s': %s\n\n",chnc_path,bc_result_str(r));return false;}
        for(int i=0;i<nf;i++) if(fns[i]->exported&&parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i];
        free(fns);return true;
    }
    if(file_exists(src_path)){
        if(already_imported(src_path)) return true;
        mark_imported(src_path); mark_imported(base);
        return compile_and_import(src_path, parent_C);
    }
    return false;
}

static bool do_import(const char *rel_path, Compiler *parent_C){
    
    char resolved[1024];
    resolve_path(parent_C->source_file,rel_path,resolved,sizeof(resolved));
    char base[1024]; strip_ext(resolved,base,sizeof(base));

    
    const char *sl2=strrchr(rel_path,'/');
    const char *dot2=strrchr(rel_path,'.');
    bool bare_name = !sl2 && !(dot2 && dot2>rel_path);

    if(!bare_name){
        
        if(already_imported(resolved)||already_imported(base)) return true;
        char fn_p[1100],cn_p[1100],sr_p[1100];
        snprintf(fn_p,sizeof(fn_p),"%s.function",base);
        snprintf(cn_p,sizeof(cn_p),"%s.chnc",    base);
        snprintf(sr_p,sizeof(sr_p),"%s.chn",     base);
        bool has_ext=dot2&&(!sl2||dot2>sl2);
        if(!has_ext) snprintf(sr_p,sizeof(sr_p),"%s.chn",resolved);
        else strncpy(sr_p,resolved,sizeof(sr_p)-1);
        if(file_exists(fn_p)){
            mark_imported(fn_p);mark_imported(base);
            FunctionObject **fns;int nf;
            BCResult r=bc_read_functions(fn_p,&fns,&nf);
            if(r!=BC_OK){fprintf(stderr,"\033[1;31merror:\033[0m load '%s': %s\n\n",fn_p,bc_result_str(r));return false;}
            for(int i=0;i<nf;i++) if(parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i];
            free(fns);return true;
        }
        if(file_exists(cn_p)){
            mark_imported(cn_p);mark_imported(base);
            Chunk dummy;FunctionObject **fns;int nf;
            BCResult r=bc_read_program(cn_p,&dummy,&fns,&nf);
            if(r!=BC_OK){fprintf(stderr,"\033[1;31merror:\033[0m load '%s': %s\n\n",cn_p,bc_result_str(r));return false;}
            for(int i=0;i<nf;i++) if(fns[i]->exported&&parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i];
            free(fns);return true;
        }
        if(file_exists(sr_p)){
            mark_imported(sr_p);mark_imported(base);
            return compile_and_import(sr_p,parent_C);
        }
        fprintf(stderr,"\033[1;31merror:\033[0m cannot find import '\033[1;36m%s\033[0m'\n       tried: %s, %s, %s\n\n",rel_path,sr_p,fn_p,cn_p);
        return false;
    }

    
    if(search_chn_libs(rel_path,parent_C)) return true;

    fprintf(stderr,"\033[1;31merror:\033[0m cannot find module '\033[1;36m%s\033[0m'\n"
            "       searched: local dir, chn-libs/ up the tree, %s/chn-libs/\n\n",
            rel_path, chn_bin_dir[0]?chn_bin_dir:"<no binary dir>");
    return false;
}

static bool compile_and_import(const char *src, Compiler *parent_C){
    char *source=read_file(src); if(!source) return false;
    const char *pf=g_source_file,*ps=g_source_code;
    error_init(src,source);
    Parser P;parser_init(&P,source);
    ASTNode *ast=parser_parse(&P);
    if(g_had_error){ast_free(ast);free(source);error_init(pf,ps);return false;}
    Compiler C;compiler_init(&C,src);
    C.global_count = parent_C->global_count;
    C.top_chunk.var_count = C.global_count;
    for(int i=0;i<parent_C->import_count&&C.import_count<MAX_FUNCS;i++) C.imports[C.import_count++]=parent_C->imports[i];
    C.import_handler=chn_import_handler;
    bool ok=compiler_compile(&C,ast);
    ast_free(ast);free(source);
    
    if(ok&&!C.had_error){
        parent_C->global_count = C.global_count;
        parent_C->top_chunk.var_count = C.global_count;
        for(int i=0;i<C.func_count;i++) if(C.functions[i]->exported&&parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=C.functions[i];
    }
    error_init(pf,ps);
    return ok&&!C.had_error;
}


static bool compile_source(const char *filepath,const char *source,Compiler *C){
    error_init(filepath,source);
    Parser P;parser_init(&P,source);
    ASTNode *ast=parser_parse(&P);
    if(g_had_error){ast_free(ast);return false;}
    compiler_init(C,filepath);
    C->import_handler=do_import;
    chn_import_handler=do_import;
    bool ok=compiler_compile(C,ast);
    ast_free(ast);
    return ok&&!C->had_error&&!g_had_error;
}

static int run_chunk(Chunk *ch){
    VM vm;vm_init(&vm);
    VMResult res=vm_run(&vm,ch);
    vm_free(&vm);
    return (res==VM_OK&&!g_had_error)?0:1;
}

typedef enum{MODE_RUN,MODE_DISASM,MODE_AST,MODE_COMPILE_ONLY}RunMode;

static int run_source_file(const char *filepath,RunMode mode,const char *out_path,bool func_only){
    char *source=read_file(filepath);if(!source) return 1;

    if(mode==MODE_AST){
        error_init(filepath,source);
        Parser P;parser_init(&P,source);
        ASTNode *ast=parser_parse(&P);
        if(!g_had_error){printf("=== AST: %s ===\n",filepath);ast_print(ast,0);}
        ast_free(ast);free(source);return g_had_error?1:0;
    }
    Compiler C;
    if(!compile_source(filepath,source,&C)){free(source);return 1;}
    free(source);

    if(mode==MODE_DISASM){
        chunk_disasm(&C.top_chunk,filepath);
        for(int i=0;i<C.func_count;i++){char lbl[320];snprintf(lbl,sizeof(lbl),"%s func %s()",visibility_name(C.functions[i]->visibility),C.functions[i]->name);chunk_disasm(&C.functions[i]->chunk,lbl);}
        return 0;
    }
    if(mode==MODE_COMPILE_ONLY){
        char auto_path[1024];const char *wpath=out_path;
        if(!wpath){strip_ext(filepath,auto_path,sizeof(auto_path));
            strncat(auto_path,func_only?".function":".chnc",sizeof(auto_path)-strlen(auto_path)-1);
            wpath=auto_path;}
        BCResult r=func_only?bc_write_functions(wpath,&C,0):bc_write_program(wpath,&C,0);
        if(r!=BC_OK){fprintf(stderr,"\033[1;31merror:\033[0m write '%s': %s\n\n",wpath,bc_result_str(r));return 1;}
        int exported=0;for(int i=0;i<C.func_count;i++) if(C.functions[i]->exported) exported++;
        printf("\033[1;32m✓\033[0m  compiled \033[1;36m%s\033[0m  →  \033[1;33m%s\033[0m\n",filepath,wpath);
        if(func_only) printf("    \033[2m%d exported function(s) packed\033[0m\n",exported);
        else printf("    \033[2m%d function(s)  |  %d instruction(s)  |  3-layer obfuscation applied\033[0m\n",C.func_count,C.top_chunk.code_len);
        return 0;
    }
    return run_chunk(&C.top_chunk);
}

static int run_chnc_file(const char *path){
    gc_init();
    Chunk top;FunctionObject **fns;int nf;
    BCResult r=bc_read_program(path,&top,&fns,&nf);
    if(r!=BC_OK){fprintf(stderr,"\033[1;31merror:\033[0m load '%s': %s\n\n",path,bc_result_str(r));return 1;}
    free(fns);
    error_init(path,"");
    int res=run_chunk(&top);
    gc_free_all();return res;
}


static void repl(void) {
    printf("Chidnat Open Source, Learn syntax through https://github.com/JeckAsChristopher/chidnat\n");
    char line_buf[4096];
    for(;;){
        printf("\033[1;32mchn\033[0m> ");fflush(stdout);
        if(!fgets(line_buf,sizeof(line_buf),stdin)){printf("\n");break;}
        size_t len=strlen(line_buf);if(len>0&&line_buf[len-1]=='\n') line_buf[--len]='\0';
        if(!strcmp(line_buf,":q")||!strcmp(line_buf,"exit")||!strcmp(line_buf,"quit")) break;
        RunMode mode=MODE_RUN;const char *src=line_buf;
        if(!strncmp(line_buf,":ast ",5)){mode=MODE_AST;src=line_buf+5;}
        if(!strncmp(line_buf,":dis ",5)){mode=MODE_DISASM;src=line_buf+5;}
        imported_count=0;func_registry_count=0;gc_init();
        char *dup=strdup(src);
        if(mode==MODE_AST){error_init("<repl>",dup);Parser P;parser_init(&P,dup);ASTNode *ast=parser_parse(&P);if(!g_had_error) ast_print(ast,0);ast_free(ast);}
        else{Compiler C;if(compile_source("<repl>",dup,&C)){if(mode==MODE_DISASM) chunk_disasm(&C.top_chunk,"<repl>");else run_chunk(&C.top_chunk);}}
        free(dup);gc_free_all();
    }
}

static void usage(const char *p){
    printf("\033[1;36mCHN v0.4.0\033[0m\n\n"
           "\033[1mRun:\033[0m\n"
           "  %s <file.chn>                Run source file\n"
           "  %s <file.chnc>               Run compiled bytecode\n\n"
           "\033[1mCompile:\033[0m\n"
           "  %s <file.chn> -o -c <out.chnc>       Compile to bytecode\n"
           "  %s <file.chn> -o -c --f <out.function> Export functions only\n"
           "  %s <file.chn> -o -c                  Auto-name output\n\n"
           "\033[1mDebug:\033[0m\n"
           "  --disasm / -d   Disassemble   --ast / -a   Dump AST\n\n"
           "\033[1mImport:\033[0m\n"
           "  imp \"mathlib\"    → tries mathlib.function, mathlib.chnc, mathlib.chn\n\n"
           "\033[1mObfuscation layers in .chnc / .function:\033[0m\n"
           "  1. Opcode substitution table (256-byte Fisher-Yates permutation)\n"
           "  2. Rolling XOR cipher (64-byte key from seed)\n"
           "  3. Variable-length junk block prefix\n\n",
           p,p,p,p,p);
}

int main(int argc,char **argv){
    
    {
        char self[1024]="";
        ssize_t n=readlink("/proc/self/exe",self,sizeof(self)-1);
        if(n>0){ self[n]='\0';
            char *sl=strrchr(self,'/');
            if(sl){ *sl='\0'; strncpy(chn_bin_dir,self,sizeof(chn_bin_dir)-1); }
        }
    }

    if(argc==1){repl();return 0;}

    RunMode mode=MODE_RUN;
    const char *filepath=NULL,*out_path=NULL;
    bool func_only=false,want_out=false;

    for(int i=1;i<argc;i++){
        const char *a=argv[i];
        if(!strcmp(a,"--help")||!strcmp(a,"-h")){usage(argv[0]);return 0;}
        else if(!strcmp(a,"--disasm")||!strcmp(a,"-d")) mode=MODE_DISASM;
        else if(!strcmp(a,"--ast")   ||!strcmp(a,"-a")) mode=MODE_AST;
        else if(!strcmp(a,"-o")) want_out=true;
        else if(!strcmp(a,"-c")){
            if(!want_out){fprintf(stderr,"\033[1;31merror:\033[0m use '-o -c <out>' to compile\n       example: %s file.chn -o -c file.chnc\n\n",argv[0]);return 1;}
            mode=MODE_COMPILE_ONLY;
            
            if(i+1<argc&&!strcmp(argv[i+1],"--f")){i++;func_only=true;if(i+1<argc&&argv[i+1][0]!='-') out_path=argv[++i];}
            else if(i+1<argc&&argv[i+1][0]!='-') out_path=argv[++i];
        }
        else if(!strcmp(a,"--f")) func_only=true;
        else if(a[0]!='-') filepath=a;
        else{fprintf(stderr,"\033[1;31merror:\033[0m unknown option '%s'\n\n",a);return 1;}
    }

    if(!filepath){fprintf(stderr,"\033[1;31merror:\033[0m no input file\n");usage(argv[0]);return 1;}

    const char *dot=strrchr(filepath,'.');
    bool is_chnc=dot&&!strcmp(dot,".chnc");
    bool is_func=dot&&!strcmp(dot,".function");

    if(is_chnc||is_func){
        if(mode==MODE_COMPILE_ONLY){fprintf(stderr,"\033[1;31merror:\033[0m cannot re-compile a bytecode file\n\n");return 1;}
        if(is_func){fprintf(stderr,"\033[1;31merror:\033[0m '%s' is a function library, not a runnable program\n       hint: import it with 'imp \"%s\"'\n\n",filepath,filepath);return 1;}
        return run_chnc_file(filepath);
    }
    return run_source_file(filepath,mode,out_path,func_only);
}
