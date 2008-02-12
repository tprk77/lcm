#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "lcmgen.h"
#include "sprintfalloc.h"

#define INDENT(n) (4*(n))

#define emit_start(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); } while (0)
#define emit_continue(...) do { fprintf(f, __VA_ARGS__); } while (0)
#define emit_end(...) do { fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)
#define emit(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)

#define FLAG_NONE 0

// flags for emit_c_array_loops_start
#define FLAG_EMIT_MALLOCS 1

// flags for emit_c_array_loops_end
#define FLAG_EMIT_FREES   2

static inline int imax(int a, int b)
{
    return (a > b) ? a : b;
}

static char *dots_to_underscores(const char *s)
{
    char *p = strdup(s);

    for (char *t=p; *t!=0; t++)
        if (*t == '.')
            *t = '_';

    return p;
}


static void emit_auto_generated_warning(FILE *f)
{
    fprintf(f, 
            "/** THIS IS AN AUTOMATICALLY GENERATED FILE.  DO NOT MODIFY\n"
            " * BY HAND!!\n"
            " *\n"
            " * Generated by LCM\n"
            " **/\n\n");
}


// Some types do not have a 1:1 mapping from lcm types to native C
// storage types. Do not free the string pointers returned by this
// function.
static const char *map_type_name(const char *t)
{
    if (!strcmp(t,"boolean"))
        return "int8_t";

    if (!strcmp(t,"string"))
        return "char*";

    if (!strcmp(t,"byte"))
        return "uint8_t";

    return dots_to_underscores (t);
}

void setup_c_options(getopt_t *gopt)
{
//    getopt_add_bool   (gopt, 0, "clcfuncs",   0,        "Add LC publish/subscribe convenience function");
    getopt_add_string (gopt, 0, "c-cpath",    ".",      "Location for .c files");
    getopt_add_string (gopt, 0, "c-hpath",    ".",      "Location for .h files");
    getopt_add_string (gopt, 0, "cinclude",   "",       "Generated #include lines reference this folder");
}

/** Emit output that is common to every header file **/
static void emit_header_top(lcmgen_t *lcm, FILE *f, char *name)
{
    emit_auto_generated_warning(f);
    
    fprintf(f, "#include <stdint.h>\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <lcm/lcm_coretypes.h>\n");
//    fprintf(f, "#include \"%s%slcm_lib.h\"\n",
//            getopt_get_string(lcm->gopt, "cinclude"),
//            strlen(getopt_get_string(lcm->gopt, "cinclude"))>0 ? "/" : "");

//    if( getopt_get_bool(lcm->gopt, "clcfuncs") ) {
        fprintf(f, "#include <lcm/lcm.h>\n");
//    }
    fprintf(f, "\n");
        
    fprintf(f, "#ifndef _%s_h\n", name);
    fprintf(f, "#define _%s_h\n", name);
    fprintf(f, "\n");
    
    fprintf(f, "#ifdef __cplusplus\n");
    fprintf(f, "extern \"C\" {\n");
    fprintf(f, "#endif\n");
    fprintf(f, "\n");
    
}

/** Emit output that is common to every header file **/
static void emit_header_bottom(lcmgen_t *lcm, FILE *f)
{
    fprintf(f, "#ifdef __cplusplus\n");
    fprintf(f, "}\n");
    fprintf(f, "#endif\n");
    fprintf(f, "\n");
    fprintf(f, "#endif\n");
}

/** Emit header file output specific to a particular type of struct. **/
static void emit_header_struct(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    for (unsigned int i = 0; i < g_ptr_array_size(ls->members); i++) {
        lcm_member_t *lm = g_ptr_array_index(ls->members, i);
        
        if (!lcm_is_primitive_type(lm->type->typename)) {
            char *other_tn = dots_to_underscores (lm->type->typename);
            fprintf(f, "#include \"%s%s%s.h\"\n",
                    getopt_get_string(lcm->gopt, "cinclude"),
                    strlen(getopt_get_string(lcm->gopt, "cinclude"))>0 ? "/" : "",
                    other_tn);
            free (other_tn);
        }
    }

    emit(0, "typedef struct _%s %s;", tn_, tn_);
    emit(0, "struct _%s", tn_);
    emit(0, "{");

    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = g_ptr_array_index(ls->members, m);
//        char *tname = dots_to_underscores (lm->

        int ndim = g_ptr_array_size(lm->dimensions);
        if (ndim == 0) {
            emit(1, "%-10s %s;", map_type_name(lm->type->typename), 
                    lm->membername);
        } else {
            if (lcm_is_constant_size_array(lm)) {
                emit_start(1, "%-10s %s", map_type_name(lm->type->typename), lm->membername);
                for (unsigned int d = 0; d < ndim; d++) {
                    lcm_dimension_t *ld = g_ptr_array_index(lm->dimensions, d);
                    emit_continue("[%s]", ld->size);
                }
                emit_end(";");
            } else {
                emit_start(1, "%-10s ", map_type_name(lm->type->typename));
                for (unsigned int d = 0; d < ndim; d++) 
                    emit_continue("*");
                emit_end("%s;", lm->membername);
            }
        }
    }
    emit(0, "};");
    emit(0, " ");
}

static void emit_header_prototypes(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"%s   *%s_copy(const %s *p);", tn_, tn_, tn_);
    emit(0,"void %s_destroy(%s *p);", tn_, tn_);
    emit(0,"");

//        if (getopt_get_bool(lcmgen->gopt, "clcfuncs")) {
    emit(0,"typedef struct _%s_subscription_t %s_subscription_t;", tn_, tn_);
    emit(0,"typedef void (*%s_handler_t)(const lcm_recv_buf_t *rbuf, \n"
           "             const char *channel, const %s *msg, void *user);", 
           tn_, tn_);
    emit(0,"");
    emit(0,"int %s_publish(lcm_t *lcm, const char *channel, const %s *p);", tn_, tn_);
    emit(0,"%s_subscription_t* %s_subscribe (lcm_t *lcm, const char *channel, %s_handler_t f, void *userdata);", tn_, tn_, tn_);
    emit(0,"int %s_unsubscribe(lcm_t *lcm, %s_subscription_t* hid);", tn_, tn_);
    emit(0,"");
    emit(0,"int  %s_encode(void *buf, int offset, int maxlen, const %s *p);", tn_, tn_);
    emit(0,"int  %s_decode(const void *buf, int offset, int maxlen, %s *p);", tn_, tn_);
    emit(0,"int  %s_decode_cleanup(%s *p);", tn_, tn_);
    emit(0,"int  %s_encoded_size(const %s *p);", tn_, tn_);
    emit(0,"");
// }
//
    emit(0,"// LCM support functions. Users should not call these");
    emit(0,"int64_t __%s_get_hash(void);", tn_);
    emit(0,"int64_t __%s_hash_recursive(const __lcm_hash_ptr *p);", tn_);
    emit(0,"int     __%s_encode_array(void *buf, int offset, int maxlen, const %s *p, int elements);", tn_, tn_);
    emit(0,"int     __%s_decode_array(const void *buf, int offset, int maxlen, %s *p, int elements);", tn_, tn_);
    emit(0,"int     __%s_decode_array_cleanup(%s *p, int elements);", tn_, tn_);
    emit(0,"int     __%s_encoded_array_size(const %s *p, int elements);", tn_, tn_);
    emit(0,"int     __%s_clone_array(const %s *p, %s *q, int elements);", tn_, tn_, tn_);
    emit(0,"");

}

static void emit_c_struct_get_hash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn  = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0, "static int __%s_hash_computed;", tn_);
    emit(0, "static int64_t __%s_hash;", tn_);
    emit(0, " ");

    emit(0, "int64_t __%s_hash_recursive(const __lcm_hash_ptr *p)", tn_);
    emit(0, "{");
    emit(1,     "const __lcm_hash_ptr *fp;");
    emit(1,     "for (fp = p; fp != NULL; fp = fp->parent)");
    emit(2,         "if (fp->v == __%s_get_hash)", tn_);
    emit(3,              "return 0;");
    emit(0, " ");
    emit(1, "const __lcm_hash_ptr cp = { .parent = p, .v = __%s_get_hash };", tn_);
    emit(1, "(void) cp;");
    emit(0, " ");
    emit(1, "int64_t hash = 0x%016"PRIx64"LL", ls->hash);
    
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = g_ptr_array_index(ls->members, m);
        
        emit(2, " + __%s_hash_recursive(&cp)", dots_to_underscores(lm->type->typename));
    }
    emit(2,";");
    emit(0, " ");
    emit(1, "return (hash<<1) + ((hash>>63)&1);");
    emit(0, "}");
    emit(0, " ");

    emit(0, "int64_t __%s_get_hash(void)", tn_);
    emit(0, "{");
    emit(1, "if (!__%s_hash_computed) {", tn_);
    emit(2,      "__%s_hash = __%s_hash_recursive(NULL);", tn_, tn_);
    emit(2,      "__%s_hash_computed = 1;", tn_);
    emit(1,      "}");
    emit(0, " ");
    emit(1, "return __%s_hash;", tn_);
    emit(0, "}");
    emit(0, " ");
}

// Create an accessor for member lm, whose name is "n". For arrays,
// the dim'th dimension is accessed. E.g., dim=0 will have no
// additional brackets, dim=1 has [a], dim=2 has [a][b].
static char *make_accessor(lcm_member_t *lm, const char *n, int dim)
{
    char *tmp = malloc(128);

    if (g_ptr_array_size(lm->dimensions) == 0) {
        sprintf(tmp, "&(%s[element].%s)", n, lm->membername);
    } else {
        int pos = sprintf(tmp, "%s[element].%s", n, lm->membername);
        for (unsigned int d = 0; d < dim; d++) {
            pos += sprintf(&tmp[pos], "[%c]", d + 'a');
        }
    }
    return tmp;
}

static char *make_array_size(lcm_member_t *lm, const char *n, int dim)
{
    if (g_ptr_array_size(lm->dimensions) == 0)
        return sprintfalloc("1");
    else {
        lcm_dimension_t *ld = g_ptr_array_index(lm->dimensions, dim);
        switch (ld->mode) 
        {
        case LCM_CONST:
            return sprintfalloc("%s", ld->size);
        case LCM_VAR:
            return sprintfalloc("%s[element].%s", n, ld->size);
        }
    }
    assert(0);
    return NULL;
}

static void emit_c_array_loops_start(lcmgen_t *lcm, FILE *f, lcm_member_t *lm, const char *n, int flags)
{
    if (g_ptr_array_size(lm->dimensions) == 0)
        return;

    for (unsigned int i = 0; i < g_ptr_array_size(lm->dimensions) - 1; i++) {
        char var = 'a' + i;

        if (flags & FLAG_EMIT_MALLOCS) {
            char stars[1000];
            for (unsigned int s = 0; s < g_ptr_array_size(lm->dimensions) - 1 - i; s++) {
                stars[s] = '*';
                stars[s+1] = 0;
            }

            emit(2+i, "%s = (%s%s*) lcm_malloc(sizeof(%s%s) * %s);", 
                 make_accessor(lm, n, i),
                 map_type_name(lm->type->typename),
                 stars,
                 map_type_name(lm->type->typename),
                 stars, 
                 make_array_size(lm, n, i));
        }

        emit(2+i, "{ int %c;", var);
        emit(2+i, "for (%c = 0; %c < %s; %c++) {", var, var, make_array_size(lm, "p", i), var);
    }

    if (flags & FLAG_EMIT_MALLOCS) {
        emit(2 + g_ptr_array_size(lm->dimensions) - 1, "%s = (%s*) lcm_malloc(sizeof(%s) * %s);",
             make_accessor(lm, n, g_ptr_array_size(lm->dimensions) - 1),
             map_type_name(lm->type->typename),             
             map_type_name(lm->type->typename),
             make_array_size(lm, n, g_ptr_array_size(lm->dimensions) - 1));
    }
}

static void emit_c_array_loops_end(lcmgen_t *lcm, FILE *f, lcm_member_t *lm, const char *n, int flags)
{
    if (g_ptr_array_size(lm->dimensions) == 0)
        return;

    for (unsigned int i = 0; i < g_ptr_array_size(lm->dimensions) - 1; i++) {
        int indent = g_ptr_array_size(lm->dimensions) - i;
        if (flags & FLAG_EMIT_FREES) {
            char *accessor =  make_accessor(lm, "p", g_ptr_array_size(lm->dimensions) - 1 - i);
            emit(indent+1, "if (%s) free(%s);", accessor, accessor);
        }
        emit(indent, "}");
        emit(indent, "}");
    }

    if (flags & FLAG_EMIT_FREES) {
        char *accessor = make_accessor(lm, "p", 0);
        emit(2, "if (%s) free(%s);", accessor, accessor);
    }
}

static void emit_c_encode_array(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int __%s_encode_array(void *buf, int offset, int maxlen, const %s *p, int elements)", tn_, tn_);
    emit(0,"{");
    emit(1,    "int pos = 0, thislen, element;");
    emit(0," ");
    emit(1,    "for (element = 0; element < elements; element++) {");
    emit(0," ");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = g_ptr_array_index(ls->members, m);
        
        emit_c_array_loops_start(lcm, f, lm, "p", FLAG_NONE);
        
        int indent = 2+imax(0, g_ptr_array_size(lm->dimensions) - 1);
        emit(indent, "thislen = __%s_encode_array(buf, offset + pos, maxlen - pos, %s, %s);", 
             dots_to_underscores (lm->type->typename),
             make_accessor(lm, "p", g_ptr_array_size(lm->dimensions) - 1),
             make_array_size(lm, "p", g_ptr_array_size(lm->dimensions) - 1));
        emit(indent, "if (thislen < 0) return thislen; else pos += thislen;");
        
        emit_c_array_loops_end(lcm, f, lm, "p", FLAG_NONE);
        emit(0," ");
    }
    emit(1,   "}");
    emit(1, "return pos;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_encode(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int %s_encode(void *buf, int offset, int maxlen, const %s *p)", tn_, tn_);
    emit(0,"{");
    emit(1,    "int pos = 0, thislen;");
    emit(1,    "int64_t hash = __%s_get_hash();", tn_);
    emit(0," ");
    emit(1,    "thislen = __int64_t_encode_array(buf, offset + pos, maxlen - pos, &hash, 1);");
    emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
    emit(0," ");
    emit(1,    "thislen = __%s_encode_array(buf, offset + pos, maxlen - pos, p, 1);", tn_);
    emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
    emit(0," ");
    emit(1, "return pos;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_decode_array(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int __%s_decode_array(const void *buf, int offset, int maxlen, %s *p, int elements)", tn_, tn_);
    emit(0,"{");
    emit(1,    "int pos = 0, thislen, element;");
    emit(0," ");
    emit(1,    "for (element = 0; element < elements; element++) {");
    emit(0," ");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = g_ptr_array_index(ls->members, m);
     
        emit_c_array_loops_start(lcm, f, lm, "p", lcm_is_constant_size_array(lm) ? FLAG_NONE : FLAG_EMIT_MALLOCS);

        int indent = 2+imax(0, g_ptr_array_size(lm->dimensions) - 1);
        emit(indent, "thislen = __%s_decode_array(buf, offset + pos, maxlen - pos, %s, %s);", 
             dots_to_underscores (lm->type->typename),
             make_accessor(lm, "p", g_ptr_array_size(lm->dimensions) - 1),
             make_array_size(lm, "p", g_ptr_array_size(lm->dimensions) - 1));
        emit(indent, "if (thislen < 0) return thislen; else pos += thislen;");

        emit_c_array_loops_end(lcm, f, lm, "p", FLAG_NONE);
        emit(0," ");
    }
    emit(1,   "}");
    emit(1, "return pos;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_decode_array_cleanup(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int __%s_decode_array_cleanup(%s *p, int elements)", tn_, tn_);
    emit(0,"{");
    emit(1,    "int element;");
    emit(1,    "for (element = 0; element < elements; element++) {");
    emit(0," ");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = g_ptr_array_index(ls->members, m);
     
        emit_c_array_loops_start(lcm, f, lm, "p", FLAG_NONE);

        int indent = 2+imax(0, g_ptr_array_size(lm->dimensions) - 1);
        emit(indent, "__%s_decode_array_cleanup(%s, %s);", 
             dots_to_underscores (lm->type->typename),
             make_accessor(lm, "p", g_ptr_array_size(lm->dimensions) - 1),
             make_array_size(lm, "p", g_ptr_array_size(lm->dimensions) - 1));

        emit_c_array_loops_end(lcm, f, lm, "p", lcm_is_constant_size_array(lm) ? FLAG_NONE : FLAG_EMIT_FREES);
        emit(0," ");
    }
    emit(1,   "}");
    emit(1, "return 0;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_decode(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int %s_decode(const void *buf, int offset, int maxlen, %s *p)", tn_, tn_);
    emit(0,"{");
    emit(1,    "int pos = 0, thislen;");
    emit(1,    "int64_t hash = __%s_get_hash();", tn_);
    emit(0," ");
    emit(1,    "int64_t this_hash;");
    emit(1,    "thislen = __int64_t_decode_array(buf, offset + pos, maxlen - pos, &this_hash, 1);");
    emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
    emit(1,    "if (this_hash != hash) return -1;");
    emit(0," ");
    emit(1,    "thislen = __%s_decode_array(buf, offset + pos, maxlen - pos, p, 1);", tn_);
    emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
    emit(0," ");
    emit(1, "return pos;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_decode_cleanup(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int %s_decode_cleanup(%s *p)", tn_, tn_);
    emit(0,"{");
    emit(1, "return __%s_decode_array_cleanup(p, 1);", tn_);
    emit(0,"}");
    emit(0," ");
}

static void emit_c_encoded_array_size(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int __%s_encoded_array_size(const %s *p, int elements)", tn_, tn_);
    emit(0,"{");
    emit(1,"int size = 0, element;");
    emit(1,    "for (element = 0; element < elements; element++) {");
    emit(0," ");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = g_ptr_array_index(ls->members, m);
        
        emit_c_array_loops_start(lcm, f, lm, "p", FLAG_NONE);
        
        int indent = 2+imax(0, g_ptr_array_size(lm->dimensions) - 1);
        emit(indent, "size += __%s_encoded_array_size(%s, %s);",
             dots_to_underscores (lm->type->typename),
             make_accessor(lm, "p", g_ptr_array_size(lm->dimensions) - 1),
             make_array_size(lm, "p", g_ptr_array_size(lm->dimensions) - 1));
        
        emit_c_array_loops_end(lcm, f, lm, "p", FLAG_NONE);
        emit(0," ");
    }
    emit(1,"}");
    emit(1, "return size;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_encoded_size(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int %s_encoded_size(const %s *p)", tn_, tn_);
    emit(0,"{");
    emit(1, "return 8 + __%s_encoded_array_size(p, 1);", tn_);
    emit(0,"}");
    emit(0," ");
}

static void emit_c_clone_array(lcmgen_t *lcm, FILE *f, lcm_struct_t *lr)
{
    char *tn = lr->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    emit(0,"int __%s_clone_array(const %s *p, %s *q, int elements)", tn_, tn_, tn_);
    emit(0,"{");
    emit(1,    "int element;");
    emit(1,    "for (element = 0; element < elements; element++) {");
    emit(0," ");
    for (unsigned int m = 0; m < g_ptr_array_size(lr->members); m++) {
        lcm_member_t *lm = g_ptr_array_index(lr->members, m);
        
        emit_c_array_loops_start(lcm, f, lm, "q", lcm_is_constant_size_array(lm) ? FLAG_NONE : FLAG_EMIT_MALLOCS);
        
        int indent = 2+imax(0, g_ptr_array_size(lm->dimensions) - 1);
        emit(indent, "__%s_clone_array(%s, %s, %s);",
             dots_to_underscores (lm->type->typename),
             make_accessor(lm, "p", g_ptr_array_size(lm->dimensions) - 1),
             make_accessor(lm, "q", g_ptr_array_size(lm->dimensions) - 1),
             make_array_size(lm, "p", g_ptr_array_size(lm->dimensions) - 1));
        
        emit_c_array_loops_end(lcm, f, lm, "p", FLAG_NONE);
        emit(0," ");
    }
    emit(1,   "}");
    emit(1,   "return 0;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_copy(lcmgen_t *lcm, FILE *f, lcm_struct_t *lr)
{
    char *tn = lr->structname->typename;
    char *tn_ = dots_to_underscores(tn);
    
    emit(0,"%s *%s_copy(const %s *p)", tn_, tn_, tn_);
    emit(0,"{");
    emit(1,    "%s *q = (%s*) malloc(sizeof(%s));", tn_, tn_, tn_);
    emit(1,    "__%s_clone_array(p, q, 1);", tn_);
    emit(1,    "return q;");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_destroy(lcmgen_t *lcm, FILE *f, lcm_struct_t *lr)
{
    char *tn = lr->structname->typename;
    char *tn_ = dots_to_underscores(tn);
    
    emit(0,"void %s_destroy(%s *p)", tn_, tn_);
    emit(0,"{");
    emit(1,    "__%s_decode_array_cleanup(p, 1);", tn_);
    emit(1,    "free(p);");
    emit(0,"}");
    emit(0," ");
}

static void emit_c_struct_publish(lcmgen_t *lcm, FILE *f, lcm_struct_t *lr)
{
    char *tn = lr->structname->typename;
    char *tn_ = dots_to_underscores(tn);
    fprintf(f, 
            "int %s_publish(lcm_t *lc, const char *channel, const %s *p)\n"
            "{\n"
            "      int max_data_size = %s_encoded_size (p);\n"
            "      uint8_t *buf = (uint8_t*) malloc (max_data_size);\n"
            "      if (!buf) return -1;\n"
            "      int data_size = %s_encode (buf, 0, max_data_size, p);\n"
            "      if (data_size < 0) {\n"
            "          free (buf);\n"
            "          return data_size;\n"
            "      }\n"
            "      int status = lcm_publish (lc, channel, buf, data_size);\n"
            "      free (buf);\n"
            "      return status;\n"
            "}\n\n", tn_, tn_, tn_, tn_);
}

static void emit_c_struct_subscribe(lcmgen_t *lcm, FILE *f, lcm_struct_t *lr)
{
    const char *tn = lr->structname->typename;
    char *tn_ = dots_to_underscores(tn);

    fprintf(f,
            "struct _%s_subscription_t {\n"
            "    %s_handler_t user_handler;\n"
            "    void *userdata;\n"
//            "    char *channel;\n"
            "    lcm_subscription_t *lc_h;\n"
            "};\n", tn_, tn_);
    fprintf(f,
            "static\n"
            "void %s_handler_stub (const lcm_recv_buf_t *rbuf, \n"
            "                            const char *channel, void *userdata)\n"
            "{\n"
            "    int status;\n"
            "    %s p;\n"
            "    memset(&p, 0, sizeof(%s));\n"
            "    status = %s_decode (rbuf->data, 0, rbuf->data_size, &p);\n"
            "    if (status < 0) {\n"
            "        fprintf (stderr, \"error %%d decoding %s!!!\\n\", status);\n"
            "        return;\n"
            "    }\n"
            "\n"
            "    %s_subscription_t *h = (%s_subscription_t*) userdata;\n"
            "    h->user_handler (rbuf, channel, &p, h->userdata);\n"
            "\n"
            "    %s_decode_cleanup (&p);\n"
            "}\n\n", tn_, tn_, tn_, tn_, tn_, tn_, tn_, tn_
        );

    fprintf(f,
            "%s_subscription_t* %s_subscribe (lcm_t *lcm, \n"
            "                    const char *channel, \n"
            "                    %s_handler_t f, void *userdata)\n"
            "{\n"
//            "    int chan_len = strlen (channel) + 1;\n"
            "    %s_subscription_t *n = (%s_subscription_t*)\n"
            "                       malloc(sizeof(%s_subscription_t));\n"
            "    n->user_handler = f;\n"
            "    n->userdata = userdata;\n"
//            "    n->channel = (char*) malloc (chan_len);\n"
//            "    memcpy (n->channel, channel, chan_len);\n"
            "    n->lc_h = lcm_subscribe (lcm, channel, \n"
            "                                 %s_handler_stub, n);\n"
            "    if (n->lc_h == NULL) {\n"
            "        fprintf (stderr,\"couldn't reg %s LCM handler!\\n\");\n"
            "        free (n);\n"
            "        return NULL;\n"
            "    }\n"
            "    return n;\n"
            "}\n\n", tn_, tn_, tn_, tn_, tn_, tn_, tn_, tn_
        );

    fprintf(f,
            "int %s_unsubscribe(lcm_t *lcm, %s_subscription_t* hid)\n"
            "{\n"
            "    int status = lcm_unsubscribe (lcm, hid->lc_h);\n"
            "    if (0 != status) {\n"
            "        fprintf(stderr, \n"
            "           \"couldn't unsubscribe %s_handler %%p!\\n\", hid);\n"
            "        return -1;\n"
            "    }\n"
//            "    free (hid->channel);\n"
            "    free (hid);\n"
            "    return 0;\n"
            "}\n\n", tn_, tn_, tn_
        );
}

int emit_enum(lcmgen_t *lcmgen, lcm_enum_t *le)
{
    char *tn = le->enumname->typename;
    char *tn_ = dots_to_underscores(tn);
    char *header_name = sprintfalloc("%s/%s.h", getopt_get_string(lcmgen->gopt, "c-hpath"), tn_);
    char *c_name      = sprintfalloc("%s/%s.c", getopt_get_string(lcmgen->gopt, "c-cpath"), tn_);

    // ENUM header file
    if (lcm_needs_generation(lcmgen, le->lcmfile, header_name)) {
        FILE *f = fopen(header_name, "w");
        if (f == NULL)
            return -1;
        
        emit_header_top(lcmgen, f, tn_);

        char *tn_upper = g_ascii_strup (tn_, strlen (tn_));

        ///////////////////////////////////////////////////////////////////
        // the enum declaration itself
        emit(0, "enum _%s {", tn_);
        for (unsigned int i = 0; i < g_ptr_array_size(le->values); i++) {
            lcm_enum_value_t *lev = g_ptr_array_index(le->values, i);
            emit(1," %s_%s = %d%s", 
                    tn_upper,
                    lev->valuename, 
                    lev->value, i==(g_ptr_array_size(le->values)-1) ? "" : ",");
        }
    
        free (tn_upper);

        emit(0, "};");
        emit(0, " ");

        emit(0, "typedef enum _%s %s;", tn_, tn_);
        emit(0, " ");

        ///////////////////////////////////////////////////////////////////

        emit(0, "static inline int64_t __%s_hash_recursive(const __lcm_hash_ptr *p)", tn_);
        emit(0, "{");
        emit(1,    "return 0x%016"PRIx64"LL;", le->hash);
        emit(0, "}");
        emit(0, " ");

        emit(0, "static inline int64_t __%s_get_hash()", tn_);
        emit(0, "{");
        emit(1,    "return 0x%016"PRIx64"LL;", le->hash);
        emit(0, "}");
        emit(0, " ");

        // enums are always "ints", but "ints" are not always int32_t. We
        // always store an enum as an int32_t, however. Consequently, we
        // jump through some hoops here in order to allow the compiler to
        // convert from an int32_t to whatever the native size of "int"
        // is.
        emit(0, "static inline int __%s_encode_array(void *_buf, int offset, int maxlen, const %s *p, int elements)", tn_, tn_);
        emit(0, "{");
        emit(1,    "int pos = 0, thislen, element;");
        emit(1,     "for (element = 0; element < elements; element++) {");
        emit(2,         "int32_t v = (int32_t) p[element];");
        emit(2,         "thislen = __int32_t_encode_array(_buf, offset + pos, maxlen - pos, &v, 1);");
        emit(2,         "if (thislen < 0) return thislen; else pos += thislen;");
        emit(1,     "}");
        emit(1, "return thislen;");
        emit(0, "}");
        emit(0, " ");

        emit(0,"static inline int %s_encode(void *buf, int offset, int maxlen, const %s *p)", tn_, tn_);
        emit(0,"{");
        emit(1,    "int pos = 0, thislen;");
        emit(1,    "int64_t hash = 0x%016"PRIx64"LL;", le->hash);
        emit(0," ");
        emit(1,    "thislen = __int64_t_encode_array(buf, offset + pos, maxlen - pos, &hash, 1);");
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0," ");
        emit(1,    "thislen = __%s_encode_array(buf, offset + pos, maxlen - pos, p, 1);", tn_);
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0," ");
        emit(1, "return pos;");
        emit(0,"}");
        emit(0," ");

        emit(0, "static inline int __%s_decode_array(const void *_buf, int offset, int maxlen, %s *p, int elements)", tn_, tn_);
        emit(0, "{");
        emit(1,    "int pos = 0, thislen, element;");
        emit(1,     "for (element = 0; element < elements; element++) {");
        emit(2,         "int32_t v;");
        emit(2,         "thislen = __int32_t_decode_array(_buf, offset + pos, maxlen - pos, &v, 1);");
        emit(2,         "if (thislen < 0) return thislen; else pos += thislen;");
        emit(2,         "p[element] = (%s) v;", tn_);
        emit(1,     "}");
        emit(1, "return thislen;");
        emit(0, "}");
        emit(0, " ");

        emit(0, "static inline int __%s_clone_array(const %s *p, %s *q, int elements)", tn_, tn_, tn_);
        emit(0, "{");
        emit(1,    "memcpy(q, p, elements * sizeof(%s));", tn_);
        emit(1,    "return 0;");
        emit(0, "}");
        emit(0, " ");

        emit(0,"static inline int %s_decode(const void *buf, int offset, int maxlen, %s *p)", tn_, tn_);
        emit(0,"{");
        emit(1,    "int pos = 0, thislen;");
        emit(1,    "int64_t hash = 0x%016"PRIx64"LL;", le->hash);
        emit(0," ");
        emit(1,    "int64_t this_hash;");
        emit(1,    "thislen = __int64_t_decode_array(buf, offset + pos, maxlen - pos, &this_hash, 1);");
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(1,    "if (this_hash != hash) return -1;");
        emit(0," ");
        emit(1,    "thislen = __%s_decode_array(buf, offset + pos, maxlen - pos, p, 1);", tn_);
        emit(1,    "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0," ");
        emit(1,   "return pos;");
        emit(0,"}");
        emit(0," ");
    
        emit(0, "static inline int __%s_decode_array_cleanup(%s *in, int elements)", tn_, tn_);
        emit(0, "{");
        emit(1,    "return 0;");
        emit(0, "}");
        emit(0, " ");

        emit(0,"static inline int %s_decode_cleanup(%s *p)", tn_, tn_);
        emit(0,"{");
        emit(1,    "return 0;");
        emit(0,"}");
        emit(0," ");

        emit(0, "static inline int __%s_encoded_array_size(const %s *p, int elements)", tn_, tn_);
        emit(0, "{");
        emit(1,    "return __int32_t_encoded_array_size((const int32_t*)p, elements);");
        emit(0, "}");
        emit(0, " ");

        emit(0, "static inline int %s_encoded_size(const %s *in)", tn_, tn_);
        emit(0, "{");
        emit(1,    "return int32_t_encoded_size((const int32_t*)in);");
        emit(0, "}");
        emit(0, " ");

        emit_header_bottom(lcmgen, f); 
        fclose(f);
    }
    
    // ENUM C file
    if (lcm_needs_generation(lcmgen, le->lcmfile, c_name)) {
        FILE *f = fopen(c_name, "w");
        fprintf(f, "/** This is the .c file for an enum type. All of the declarations\n");
        fprintf(f, "  * are in the corresponding header file. This file is intentionally\n");
        fprintf(f, "  * empty, in order to allow Makefiles that expect all lcm types (even\n");
        fprintf(f, "  * enums) to have a .c file.\n");
        fprintf(f, "**/\n");
        fclose(f);
    }

    return 0;
}

int emit_struct(lcmgen_t *lcmgen, lcm_struct_t *lr)
{
    char *tn = lr->structname->typename;
    char *tn_ = dots_to_underscores(tn);
    char *header_name = sprintfalloc("%s/%s.h", getopt_get_string(lcmgen->gopt, "c-hpath"), tn_);
    char *c_name      = sprintfalloc("%s/%s.c", getopt_get_string(lcmgen->gopt, "c-cpath"), tn_);

    if (lcm_needs_generation(lcmgen, lr->lcmfile, header_name)) {
        FILE *f = fopen(header_name, "w");
        if (f == NULL)
            return -1;
        
        emit_header_top(lcmgen, f, tn_);
        emit_header_struct(lcmgen, f, lr);
        emit_header_prototypes(lcmgen, f, lr);

        emit_header_bottom(lcmgen, f);
        fclose(f);
    }
    
    // STRUCT C file
    if (lcm_needs_generation(lcmgen, lr->lcmfile, c_name)) {
        FILE *f = fopen(c_name, "w");
        if (f == NULL)
            return -1;
        
        emit_auto_generated_warning(f);
        fprintf(f, "#include <string.h>\n");
        fprintf(f, "#include \"%s%s%s.h\"\n",
                getopt_get_string(lcmgen->gopt, "cinclude"),
                strlen(getopt_get_string(lcmgen->gopt, "cinclude"))>0 ? "/" : "",
                tn_);
        fprintf(f, "\n");

        emit_c_struct_get_hash(lcmgen, f, lr);
        emit_c_encode_array(lcmgen, f, lr);
        emit_c_encode(lcmgen, f, lr);
        emit_c_encoded_array_size(lcmgen, f, lr);
        emit_c_encoded_size(lcmgen, f, lr);
        
        emit_c_decode_array(lcmgen, f, lr);
        emit_c_decode_array_cleanup(lcmgen, f, lr);
        emit_c_decode(lcmgen, f, lr);
        emit_c_decode_cleanup(lcmgen, f, lr);
        
        emit_c_clone_array(lcmgen, f, lr);
        emit_c_copy(lcmgen, f, lr);
        emit_c_destroy(lcmgen, f, lr);
        
//        if( getopt_get_bool(lcmgen->gopt, "clcfuncs" ) ) {
            emit_c_struct_publish(lcmgen, f, lr );
            emit_c_struct_subscribe(lcmgen, f, lr );
//        }
        
        fclose(f);
    }

    return 0;
}

int emit_c(lcmgen_t *lcmgen)
{
    ////////////////////////////////////////////////////////////
    // ENUMS
    for (unsigned int i = 0; i < g_ptr_array_size(lcmgen->enums); i++) {

        lcm_enum_t *le = g_ptr_array_index(lcmgen->enums, i);
        if (emit_enum(lcmgen, le))
            return -1;
    }
    
    ////////////////////////////////////////////////////////////
    // STRUCTS
    for (unsigned int i = 0; i < g_ptr_array_size(lcmgen->structs); i++) {
        lcm_struct_t *lr = g_ptr_array_index(lcmgen->structs, i);

        if (emit_struct(lcmgen, lr))
            return -1;
    }

    return 0;
}
