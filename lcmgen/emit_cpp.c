#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#ifdef WIN32
#define __STDC_FORMAT_MACROS			// Enable integer types
#endif
#include <inttypes.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "lcmgen.h"
#include "sprintfalloc.h"

#define INDENT(n) (4*(n))

#define emit_start(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); } while (0)
#define emit_continue(...) do { fprintf(f, __VA_ARGS__); } while (0)
#define emit_end(...) do { fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)
#define emit(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)

static inline int imax(int a, int b)
{
    return (a > b) ? a : b;
}

static char *
dots_to_underscores(const char *s)
{
    char *p = strdup(s);

    for (char *t=p; *t!=0; t++)
        if (*t == '.')
            *t = '_';

    return p;
}

static char *
dots_to_double_colons(const char *s)
{
    // allocate the maximum possible amount of space needed
    char* p = (char*) calloc(1, 2 * strlen(s) + 1);
    char* q = p;

    for (const char *t=s; *t!=0; t++) {
        if (*t == '.') {
            *q = ':';
            q++;
            *q = ':';
        } else 
            *q = *t;
        q++;
    }

    return p;
}

static char *dots_to_slashes(const char *s)
{
    char *p = strdup(s);
    for (char *t=p; *t!=0; t++)
        if (*t == '.')
            *t = G_DIR_SEPARATOR;
    return p;
}

static void make_dirs_for_file(const char *path)
{
#ifdef WIN32
    char *dirname = g_path_get_dirname(path);
    g_mkdir_with_parents(dirname, 0755);
    g_free(dirname);
#else
    int len = strlen(path);
    for (int i = 0; i < len; i++) {
        if (path[i]=='/') {
            char *dirpath = (char *) malloc(i+1);
            strncpy(dirpath, path, i);
            dirpath[i]=0;

            mkdir(dirpath, 0755);
            free(dirpath);

            i++; // skip the '/'
        }
    }
#endif
}

static const char * dim_size_prefix(const char *dim_size) {
    char *eptr = NULL;
    long asdf = strtol(dim_size, &eptr, 0);
    (void) asdf;  // suppress compiler warnings
    if(*eptr == '\0')
        return "";
    else
        return "this->";
}

// Some types do not have a 1:1 mapping from lcm types to native C
// storage types. Do not free the string pointers returned by this
// function.
static char *
map_type_name(const char *t)
{
    if (!strcmp(t,"boolean"))
        return strdup("int8_t");

    if (!strcmp(t,"string"))
        return strdup("std::string");

    if (!strcmp(t,"byte"))
        return strdup("uint8_t");

    return dots_to_underscores (t);
}

void setup_cpp_options(getopt_t *gopt)
{
    getopt_add_string (gopt, 0, "cpp-cpath",    ".",      "Location for .cpp files");
    getopt_add_string (gopt, 0, "cpp-hpath",    ".",      "Location for .hpp files");
    getopt_add_string (gopt, 0, "cpp-include",   "",       "Generated #include lines reference this folder");
}

static void emit_auto_generated_warning(FILE *f)
{
    fprintf(f, 
            "/** THIS IS AN AUTOMATICALLY GENERATED FILE.  DO NOT MODIFY\n"
            " * BY HAND!!\n"
            " *\n"
            " * Generated by lcm-gen\n"
            " **/\n\n");
}

static void
emit_package_namespace_start(lcmgen_t* lcmgen, FILE* f, lcm_struct_t* ls)
{
    // output namespace declaration
    char **namespaces = g_strsplit(ls->structname->lctypename, ".", 0);
    for(int nsind=0; namespaces[nsind] && namespaces[nsind+1]; nsind++) {
        emit(0, "namespace %s \n{", namespaces[nsind]);
    }
    g_strfreev(namespaces);
}

static void
emit_package_namespace_close(lcmgen_t* lcmgen, FILE* f, lcm_struct_t* ls)
{
    char **namespaces = g_strsplit(ls->structname->lctypename, ".", 0);
    for(int nsind=0; namespaces[nsind] && namespaces[nsind+1]; nsind++) {
        emit(0, "}\n");
    }
    g_strfreev(namespaces);
}

/** Emit header file **/
static void emit_header_start(lcmgen_t *lcmgen, FILE *f, lcm_struct_t *ls)
{
    char *tn = ls->structname->lctypename;
    char *sn = ls->structname->shortname;
    char *tn_ = dots_to_underscores(tn);

    emit_auto_generated_warning(f);
    
    fprintf(f, "#include <lcm/lcm_coretypes.h>\n");
    fprintf(f, "\n");
    fprintf(f, "#ifndef __%s_hpp__\n", tn_);
    fprintf(f, "#define __%s_hpp__\n", tn_);
    fprintf(f, "\n");
    
    // do wew need to #include <vector> ?
    for (unsigned int mind = 0; mind < g_ptr_array_size(ls->members); mind++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, mind);
        if (g_ptr_array_size(lm->dimensions) != 0 && !lcm_is_constant_size_array(lm)) {
            emit(0, "#include <vector>");
            break;
        }
    }

    // include header files for other LCM types
    for (unsigned int mind = 0; mind < g_ptr_array_size(ls->members); mind++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, mind);
        
        if (!lcm_is_primitive_type(lm->type->lctypename)) {
            char *other_tn = dots_to_slashes (lm->type->lctypename);
            emit(0, "#include \"%s%s%s.hpp\"",
                    getopt_get_string(lcmgen->gopt, "cpp-include"),
                    strlen(getopt_get_string(lcmgen->gopt, "cpp-include"))>0 ? G_DIR_SEPARATOR_S : "",
                    other_tn);
            free(other_tn);
        }
    }

    fprintf(f, "\n");
    emit_package_namespace_start(lcmgen, f, ls);

    // define the class
    emit(0, "\nclass %s", sn);
    emit(0, "{");

    // data members
    if(g_ptr_array_size(ls->members)) {
        emit(1, "public:");
        for (unsigned int mind = 0; mind < g_ptr_array_size(ls->members); mind++) {
            lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, mind);

            char* mapped_typename = map_type_name(lm->type->lctypename);
            int ndim = g_ptr_array_size(lm->dimensions);
            if (ndim == 0) {
                emit(2, "%-10s %s;", mapped_typename, lm->membername);
            } else {
                if (lcm_is_constant_size_array(lm)) {
                    emit_start(2, "%-10s %s", map_type_name(lm->type->lctypename), lm->membername);
                    for (unsigned int d = 0; d < ndim; d++) {
                        lcm_dimension_t *ld = (lcm_dimension_t *) g_ptr_array_index(lm->dimensions, d);
                        emit_continue("[%s]", ld->size);
                    }
                    emit_end(";");
                } else {
                    emit_start(2, "");
                    for (unsigned int d = 0; d < ndim; d++) 
                        emit_continue("std::vector< ");
                    emit_continue("%s", mapped_typename);
                    for (unsigned int d = 0; d < ndim; d++) 
                        emit_continue(" >");
                    emit_end(" %s;", lm->membername);
                }
            }
            free(mapped_typename);
        }
        emit(0, "");
    }

    // constants
    if (g_ptr_array_size(ls->constants) > 0) {
        emit(1, "public:");
        for (unsigned int i = 0; i < g_ptr_array_size(ls->constants); i++) {
            lcm_constant_t *lc = (lcm_constant_t *) g_ptr_array_index(ls->constants, i);
            assert(lcm_is_legal_const_type(lc->lctypename));

            const char *suffix = "";
            if (!strcmp(lc->lctypename, "int64_t"))
              suffix = "LL";
            char* mapped_typename = map_type_name(lc->lctypename);
            emit(2, "static const %-8s %s = %s%s;", mapped_typename, 
                lc->membername, lc->val_str, suffix);
            free(mapped_typename);
        }
        emit(0, "");
    }

    emit(1, "public:");
    emit(2, "int encode(void *buf, int offset, int maxlen) const;");
    emit(2, "int getEncodedSize() const;");
    emit(2, "int decode(const void *buf, int offset, int maxlen);");
    emit(2, "static int64_t getHash();");
    emit(2, "static const char* getTypeName();");

    emit(0, "");
    emit(2, "// LCM support functions. Users should not call these");
    emit(2, "int _encodeNoHash(void *buf, int offset, int maxlen) const;");
    emit(2, "int _getEncodedSizeNoHash() const;");
    emit(2, "int _decodeNoHash(const void *buf, int offset, int maxlen);");
    emit(2, "static int64_t _computeHash(const __lcm_hash_ptr *p);");
    emit(0, "};");
    emit(0, "");

    free(tn_);
}

static void emit_encode(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char* sn = ls->structname->shortname;
    emit(0, "int %s::encode(void *buf, int offset, int maxlen) const", sn);
    emit(0, "{");
    emit(1,     "int pos = 0, tlen;");
    emit(1,     "int64_t hash = getHash();");
    emit(0, "");
    emit(1,     "tlen = __int64_t_encode_array(buf, offset + pos, maxlen - pos, &hash, 1);");
    emit(1,     "if(tlen < 0) return tlen; else pos += tlen;");
    emit(0, "");
    emit(1,     "tlen = this->_encodeNoHash(buf, offset + pos, maxlen - pos);");
    emit(1,     "if (tlen < 0) return tlen; else pos += tlen;");
    emit(0, "");
    emit(1,     "return pos;");
    emit(0, "}");
    emit(0, "");
}

static void emit_encoded_size(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char *sn = ls->structname->shortname;
    emit(0,"int %s::getEncodedSize() const", sn);
    emit(0,"{");
    emit(1, "return 8 + _getEncodedSizeNoHash();");
    emit(0,"}");
    emit(0," ");
}

static void emit_decode(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char* sn = ls->structname->shortname;
    emit(0, "int %s::decode(const void *buf, int offset, int maxlen)", sn);
    emit(0, "{");
    emit(1,     "int pos = 0, thislen;");
    emit(0, "");
    emit(1,     "int64_t msg_hash;");
    emit(1,     "thislen = __int64_t_decode_array(buf, offset + pos, maxlen - pos, &msg_hash, 1);");
    emit(1,     "if (thislen < 0) return thislen; else pos += thislen;");
    emit(1,     "if (msg_hash != getHash()) return -1;");
    emit(0, "");
    emit(1,     "thislen = this->_decodeNoHash(buf, offset + pos, maxlen - pos);");
    emit(1,     "if (thislen < 0) return thislen; else pos += thislen;");
    emit(0, "");
    emit(1,  "return pos;");
    emit(0, "}");
    emit(0, "");
}

static void emit_get_hash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char *sn  = ls->structname->shortname;
    emit(0, "int64_t %s::getHash()", sn);
    emit(0, "{");
    emit(1,     "static int64_t hash = _computeHash(NULL);");
    emit(1,     "return hash;");
    emit(0, "}");
    emit(0, "");
}

static void emit_compute_hash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char *sn  = ls->structname->shortname;
    int last_complex_member = -1;
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        if(!lcm_is_primitive_type(lm->type->lctypename))
            last_complex_member = m;
    }

    emit(0, "int64_t %s::_computeHash(const __lcm_hash_ptr *p)", sn);
    emit(0, "{");

    if(last_complex_member >= 0) {
        emit(1,     "const __lcm_hash_ptr *fp;");
        emit(1,     "for(fp = p; fp != NULL; fp = fp->parent)");
        emit(2,         "if(fp->v == %s::getHash)", sn);
        emit(3,              "return 0;");
        if(g_ptr_array_size(ls->members)) {
            emit(1, "const __lcm_hash_ptr cp = { p, (void*)%s::getHash };", sn);
        }
        emit(0, " ");
        emit(1, "int64_t hash = 0x%016"PRIx64"LL +", ls->hash);

        for (unsigned int m = 0; m <= last_complex_member; m++) {
            lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
            char* lm_tnc = dots_to_double_colons(lm->type->lctypename);
            if(!lcm_is_primitive_type(lm->type->lctypename)) {
                emit(2, " %s::_computeHash(&cp)%s", 
                        lm_tnc, 
                        (m == last_complex_member) ? ";" : " +");
            }
            free(lm_tnc);
        }
        emit(0, " ");
    } else {
        emit(1, "int64_t hash = 0x%016"PRIx64"LL;", ls->hash);
    }

    emit(1, "return (hash<<1) + ((hash>>63)&1);");
    emit(0, "}");
    emit(0, " ");
}

static void _encode_recursive(lcmgen_t* lcm, FILE* f, lcm_member_t* lm, int depth)
{
    // primitive array
    if (depth+1 == g_ptr_array_size(lm->dimensions) && 
            lcm_is_primitive_type(lm->type->lctypename) &&
            strcmp(lm->type->lctypename, "string")) {
        lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);
        emit_start(1 + depth, "tlen = __%s_encode_array(buf, offset + pos, maxlen - pos, &this->%s", 
                lm->type->lctypename, lm->membername);
        for(int i=0; i<depth; i++)
            emit_continue("[a%d]", i);
        emit_end("[0], %s%s);", dim_size_prefix(dim->size), dim->size);

        emit(1 + depth, "if(tlen < 0) return tlen; else pos += tlen;");
        return;
    }
    // 
    if(depth == g_ptr_array_size(lm->dimensions)) {
        if(!strcmp(lm->type->lctypename, "string")) {
            emit_start(1 + depth, "char* __cstr = (char*) this->%s", lm->membername);
            for(int i=0; i<depth; i++)
                emit_continue("[a%d]", i);
            emit_end(".c_str();");
            emit(1 + depth, "tlen = __string_encode_array(buf, offset + pos, maxlen - pos, &__cstr, 1);");
        } else {
            emit_start(1 + depth, "tlen = this->%s", lm->membername);
            for(int i=0; i<depth; i++)
                emit_continue("[a%d]", i);
            emit_end("._encodeNoHash(buf, offset + pos, maxlen - pos);");
        }
        emit(1 + depth, "if(tlen < 0) return tlen; else pos += tlen;");
        return;
    }

    lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);

    emit(1+depth, "for (int a%d = 0; a%d < %s%s; a%d++) {",
            depth, depth, dim_size_prefix(dim->size), dim->size, depth);

    _encode_recursive(lcm, f, lm, depth+1);

    emit(1+depth, "}");
}

static void emit_encode_nohash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char* sn = ls->structname->shortname;
    emit(0, "int %s::_encodeNoHash(void *buf, int offset, int maxlen) const", sn);
    emit(0, "{");
    if(0 == g_ptr_array_size(ls->members)) {
        emit(1,     "return 0;");
        emit(0,"}");
        emit(0," ");
        return;
    }
    emit(0, "");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);

        if (0 == g_ptr_array_size(lm->dimensions) && lcm_is_primitive_type(lm->type->lctypename)) {
            if(!strcmp(lm->type->lctypename, "string")) {
                emit(1, "char* %s_cstr = (char*) this->%s.c_str();", lm->membername, lm->membername);
                emit(1, "tlen = __string_encode_array(buf, offset + pos, maxlen - pos, &%s_cstr, 1);", 
                        lm->membername);
            } else {
            emit(1, "tlen = __%s_encode_array(buf, offset + pos, maxlen - pos, &this->%s, 1);", 
                lm->type->lctypename, lm->membername);
            }
            emit(1, "if(tlen < 0) return tlen; else pos += tlen;");
        } else {
            _encode_recursive(lcm, f, lm, 0);
        }
        
        emit(0," ");
    }
    emit(1, "return pos;");
    emit(0,"}");
    emit(0," ");
}

static void emit_encoded_size_nohash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char *sn = ls->structname->shortname;
    emit(0, "int %s::_getEncodedSizeNoHash() const", sn);
    emit(0, "{");
    if(0 == g_ptr_array_size(ls->members)) {
        emit(1,     "return 0;");
        emit(0,"}");
        emit(0," ");
        return;
    }
    emit(1,     "int enc_size = 0;");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        int ndim = g_ptr_array_size(lm->dimensions);

        if(lcm_is_primitive_type(lm->type->lctypename) &&
                strcmp(lm->type->lctypename, "string")) {
            emit_start(1, "enc_size += ");
            for(int n=0; n < ndim - 1; n++) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, n);
                emit_continue("%s%s * ", dim_size_prefix(dim->size), dim->size);
            }
            if(ndim > 0) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, ndim - 1);
                emit_end("__%s_encoded_array_size(NULL, %s%s);", 
                        lm->type->lctypename, dim_size_prefix(dim->size), dim->size);
            } else {
                emit_end("__%s_encoded_array_size(NULL, 1);", lm->type->lctypename);
            }
        } else {
            for(int n=0; n < ndim; n++) {
                lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, n);
                emit(1+n, "for (int a%d = 0; a%d < %s%s; a%d++) {",
                        n, n, dim_size_prefix(dim->size), dim->size, n);
            }
            emit_start(ndim + 1, "enc_size += this->%s", lm->membername);
            for(int i=0; i<ndim; i++)
                emit_continue("[a%d]", i);
            if(!strcmp(lm->type->lctypename, "string")) {
                emit_end(".size() + 4 + 1;");
            } else {
                emit_end("._getEncodedSizeNoHash();");
            }
            for(int n=ndim-1; n >= 0; n--) {
                emit(1 + n, "}");
            }
        }
    }
    emit(1, "return enc_size;");
    emit(0,"}");
    emit(0," ");
}

static void _decode_recursive(lcmgen_t* lcm, FILE* f, lcm_member_t* lm, int depth)
{
    // primitive array
    if (depth+1 == g_ptr_array_size(lm->dimensions) && 
        lcm_is_primitive_type(lm->type->lctypename) && 
        strcmp(lm->type->lctypename, "string")) {
        lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);

        int decode_indent = 1 + depth;
        if(!lcm_is_constant_size_array(lm)) {
            emit_start(1 + depth, "this->%s", lm->membername);
            for(int i=0; i<depth; i++)
                emit_continue("[a%d]", i);
            emit_end(".resize(%s%s);", dim_size_prefix(dim->size), dim->size);
            emit(1 + depth, "if(%s%s)", dim_size_prefix(dim->size), dim->size);
            decode_indent++;
        }

        emit_start(decode_indent, "tlen = __%s_decode_array(buf, offset + pos, maxlen - pos, &this->%s", 
                lm->type->lctypename, lm->membername);
        for(int i=0; i<depth; i++)
            emit_continue("[a%d]", i);
        emit_end("[0], %s%s);", dim_size_prefix(dim->size), dim->size);
        emit(1 + depth, "if(tlen < 0) return tlen; else pos += tlen;");
        return;
    }
    // 
    if(depth == g_ptr_array_size(lm->dimensions)) {
        if(!strcmp(lm->type->lctypename, "string")) { 
            emit(1 + depth, "int32_t __elem_len;");
            emit(1 + depth, "tlen = __int32_t_decode_array(buf, offset + pos, maxlen - pos, &__elem_len, 1);");
            emit(1 + depth, "if(tlen < 0) return tlen; else pos += tlen;");
            emit(1 + depth, "if(__elem_len > maxlen - pos) return -1;");
            emit_start(1 + depth, "this->%s", lm->membername);
            for(int i=0; i<depth; i++)
                emit_continue("[a%d]", i);
            emit_end(".assign(((const char*)buf) + offset + pos, __elem_len -  1);");
            emit(1 + depth, "pos += __elem_len;");
        } else {
            emit_start(1 + depth, "tlen = this->%s", lm->membername);
            for(int i=0; i<depth; i++)
                emit_continue("[a%d]", i);
            emit_end("._decodeNoHash(buf, offset + pos, maxlen - pos);");
            emit(1 + depth, "if(tlen < 0) return tlen; else pos += tlen;");
        }
        return;
    }

    lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, depth);

    emit_start(1+depth, "this->%s", lm->membername);
    for(int i=0; i<depth; i++) {
      emit_continue("[a%d]", i);
    }
    emit_end(".resize(%s%s);", dim_size_prefix(dim->size), dim->size);
    emit(1+depth, "for (int a%d = 0; a%d < %s%s; a%d++) {",
            depth, depth, dim_size_prefix(dim->size), dim->size, depth);

    _decode_recursive(lcm, f, lm, depth+1);

    emit(1+depth, "}");
}

static void emit_decode_nohash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char* sn = ls->structname->shortname;
    emit(0, "int %s::_decodeNoHash(const void *buf, int offset, int maxlen)", sn);
    emit(0, "{");
    if(0 == g_ptr_array_size(ls->members)) {
        emit(1,     "return 0;");
        emit(0,"}");
        emit(0," ");
        return;
    }
    emit(1,     "int pos = 0, tlen;");
    emit(0, "");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);

        if (0 == g_ptr_array_size(lm->dimensions) && lcm_is_primitive_type(lm->type->lctypename)) {
            if(!strcmp(lm->type->lctypename, "string")) { 
                emit(1, "int32_t __%s_len__;", lm->membername);
                emit(1, "tlen = __int32_t_decode_array(buf, offset + pos, maxlen - pos, &__%s_len__, 1);", lm->membername);
                emit(1, "if(tlen < 0) return tlen; else pos += tlen;");
                emit(1, "if(__%s_len__ > maxlen - pos) return -1;", lm->membername);
                emit(1, "this->%s.assign(((const char*)buf) + offset + pos, __%s_len__ - 1);", lm->membername, lm->membername);
                emit(1, "pos += __%s_len__;", lm->membername);
            } else {
                emit(1, "tlen = __%s_decode_array(buf, offset + pos, maxlen - pos, &this->%s, 1);", lm->type->lctypename, lm->membername);
                emit(1, "if(tlen < 0) return tlen; else pos += tlen;");
            }
        } else {
            _decode_recursive(lcm, f, lm, 0);
        }
        
        emit(0," ");
    }
    emit(1, "return pos;");
    emit(0, "}");
    emit(0, "");
}

int emit_cpp(lcmgen_t *lcmgen)
{
    // iterate through all defined message types
    for (unsigned int i = 0; i < g_ptr_array_size(lcmgen->structs); i++) {
        lcm_struct_t *lr = (lcm_struct_t *) g_ptr_array_index(lcmgen->structs, i);

        const char *tn = lr->structname->lctypename;
        char *tn_ = dots_to_slashes(tn);

        // compute the target filename
        char *header_name = sprintfalloc("%s%s%s.hpp", 
                getopt_get_string(lcmgen->gopt, "cpp-hpath"), 
                strlen(getopt_get_string(lcmgen->gopt, "cpp-hpath")) > 0 ? G_DIR_SEPARATOR_S : "",
                tn_);

        // generate code if needed
        if (lcm_needs_generation(lcmgen, lr->lcmfile, header_name)) {
            make_dirs_for_file(header_name);

            FILE *f = fopen(header_name, "w");
            if (f == NULL)
                return -1;

            emit_header_start(lcmgen, f, lr);
            emit_encode(lcmgen, f, lr);
            emit_decode(lcmgen, f, lr);
            emit_encoded_size(lcmgen, f, lr);
            emit_get_hash(lcmgen, f, lr);
            emit(0, "const char* %s::getTypeName()", lr->structname->shortname);
            emit(0, "{");
            emit(1,     "return \"%s\";", lr->structname->shortname);
            emit(0, "}");
            emit(0, "");

            emit_encode_nohash(lcmgen, f, lr);
            emit_decode_nohash(lcmgen, f, lr);
            emit_encoded_size_nohash(lcmgen, f, lr);
            emit_compute_hash(lcmgen, f, lr);

            emit_package_namespace_close(lcmgen, f, lr);
            emit(0, "#endif");

            fclose(f);
        }
        free(header_name);
        free(tn_);
    }

    return 0;
}
