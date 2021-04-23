#ifndef _F_OUTPUT_XCODEML_CONTEXT_H_
#define _F_OUTPUT_XCODEML_CONTEXT_H_

#include "F-datatype.h"
#include "F-ident.h"
#include "utils.h"
#include "external/klib/khash.h"

#define CURRENT_FUNCTION_STACK_MAX_SIZE 100
#define CEXPR_OPTVAL_CHARLEN 128

typedef struct type_ext_id {
    EXT_ID ep;
    struct type_ext_id *next;
} * TYPE_EXT_ID;

static const int type_desc_set = 33;
KHASH_SET_INIT_INT64(type_desc_set);

struct s_out_xcodeml_context {
	char s_timestamp[CEXPR_OPTVAL_CHARLEN];
    bool is_emitting_for_submodule;
    bool is_inside_interface;
    EXT_ID current_function_stack[CURRENT_FUNCTION_STACK_MAX_SIZE];
    int current_function_top;
    // Set containing int representation of type descriptor addresses
    khash_t(type_desc_set) * type_set;
    khash_t(type_desc_set) * tbp_set;
    TYPE_DESC type_list, type_list_tail;
    TYPE_DESC tbp_list, tbp_list_tail;
    TYPE_EXT_ID type_ext_id_list, type_ext_id_last;
    FILE *print_fp;
    bool is_outputed_module;
    bool is_emitting_module;
};

typedef struct s_out_xcodeml_context out_xcodeml_context;

void init_out_xcodeml_context(out_xcodeml_context*);
void free_out_xcodeml_context(out_xcodeml_context*);
void set_out_xcodeml_context(out_xcodeml_context*);

extern THREAD_LOCAL out_xcodeml_context* outx_ctx;

#endif /* _F_OUTPUT_XCODEML_CONTEXT_H_ */
