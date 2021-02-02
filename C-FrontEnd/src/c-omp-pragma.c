/* -*- Mode: C; c-basic-offset:2 ; indent-tabs-mode:nil ; -*- */
#include <sys/param.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

#include "c-expr.h"
#include "c-pragma.h"
#include "c-parser.h"
#include "c-const.h"
#include "c-option.h"
#include "c-omp.h"

/*
 * <OMPPragma> <string> directive_name </string> 
 *             [clauses] [body] </OMPPragma>
 * [clauses] = <list> [clause] </list>
 *   C_Front: (direcive clause1 clause2 ... )
 *
 * [data_clause] = 
 *     <list> <string> [data_clause_name] </string> [name_list] </list>
 *     [data_clause_name] = DATA_PRIVATE|OMP_DATA_SHARED|
 *               OMP_DATA_FIRSTPRIVATE|OMP_DATA_LASTPRIVATE|OMP_DATA_COPYIN|
 *               OMP_DATA_REDUCTION_***
 *     [name_list] = <list> variable ... </list>
 *  C_Front: (data_clause_name (LIST ident ... ))
 * 
 * [default_clause] = 
 *      <list> <string> OMP_DATA_DEFAULT </string> 
 *           <string> OMP_DEFAULT_*** </string> </list>
 *  C_Front: (OMP_DATA_DEFAULT (OMP_DEFAULT_*** null))
 * 
 * [if_clause] = <list> <string> OMP_DIR_IF </string> cond_expr </list>
 *  C_Front: (OMP_DIR_IF cond_expr)
 *
 * [schedule_clause] = 
 *       <list> <string> OMP_DIR_SCHEDULE </string>
 *           <list> <string> OMP_SCHED_*** </string> expr </list> </list>
 *  C_Front: (OMP_DIR_SCHEDULE (OMP_SCHED_*** expr))
 *
 * [ordered_clause] = <list> <string> OMP_DIR_ORDERED </strign> null </list>
 *  C_Front: (OMP_DIR_ORDERED null) 
 *
 * [nowait_clause] = <list> <string> OMP_DIR_NOWAIT </strign> null </list>
 *  C_Front: (OMP_DIR_NOWAIT null) 
 *
 * [num_threads_clause] = 
 *    <list> <string> OMP_DIR_NUM_THREADS </strign> expr </list>
 *  C_Front: (OMP_DIR_NUM_THREADS expr) 
 *
 */

static int parse_OMP_pragma(void);
static CExpr* parse_OMP_clauses(void);
static CExpr* parse_OMP_namelist(void);
static CExpr* parse_OMP_reduction_namelist(int *r);

static int parse_OMP_target_pragma(void);
static int parse_OMP_teams_pragma(void);
static int parse_OMP_distribute_pragma(void);
static int parse_OMP_parallel_for_SIMD_pragma(void);
static int parse_OMP_if_directive_name_modifier(int *r);

static void get_peek_token(char *head, char **token,
                           size_t *token_len, char **next,
                           int *num_peek);

#define OMP_PG_LIST(pg,args) _omp_pg_list(pg,args)

#define OMP_DATA_MAP_TO      0
#define OMP_DATA_MAP_FROM    1
#define OMP_DATA_MAP_TOFROM  2
#define OMP_DATA_MAP_ALLOC   3
#define OMP_DATA_MAP_RELEASE 4
#define OMP_DATA_MAP_DELETE  5
#define OMP_CLAUSE_DEVICE    0
#define OMP_CLAUSE_SHADOW    1

static CExpr* _omp_pg_list(int omp_code,CExpr* args)
{
  CExprOfList *lp;
  lp = allocExprOfList1(EC_UNDEF,args);
  lp->e_aux = omp_code;
  return (CExpr *)lp;
}

#define EMPTY_LIST (CExpr *)allocExprOfList(EC_UNDEF)

#ifdef not
static expv compile_OMP_SECTIONS_statement(expr x);
static void compile_OMP_pragma_clause(expr x, int pragma, int is_parallel, expv *pc, expv *dc);
static void compile_OMP_name_list _ANSI_ARGS_((expr x));
#endif

static int pg_OMP_pragma;
CExpr* pg_OMP_list;

/*
 * for OpenMP directives
 */
CExpr*
lexParsePragmaOMP(char *p, int *token) // p is buffer
{
  //skip pragma[space]omp[space]*
  p = lexSkipSpace(lexSkipWordP(lexSkipSpace(lexSkipWord(lexSkipSpace(lexSkipSharp(lexSkipSpace(p)))))));

  pg_cp = p; // set the pointer

  *token = parse_OMP_pragma();

  if(pg_OMP_list == NULL) pg_OMP_list = EMPTY_LIST;
  ((CExprOfList *)pg_OMP_list)->e_aux = pg_OMP_pragma;
  
  return pg_OMP_list;
}

int parse_OMP_pragma()
{
  int ret = PRAGMA_PREFIX; /* default */
  pg_OMP_pragma = OMP_NONE;
  pg_OMP_list = NULL;

  pg_get_token();
  if(pg_tok != PG_IDENT) goto syntax_err;

  /* parallel block directive */
  if(PG_IS_IDENT("parallel")){
    pg_get_token();
    if(pg_tok == PG_IDENT){
      if(PG_IS_IDENT("for")){	/* parallel for */
	pg_OMP_pragma = OMP_PARALLEL_FOR;
	pg_get_token();
        if(pg_tok == PG_IDENT){
          if(PG_IS_IDENT("simd")){  /* parallel for simd */
            pg_OMP_pragma = OMP_PARALLEL_LOOP_SIMD;
            pg_get_token();
            if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
            goto chk_end;
          }
        }
	if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
	goto chk_end;
      }
      if(PG_IS_IDENT("sections")){	/* parallel for */
	pg_OMP_pragma = OMP_PARALLEL_SECTIONS;
	pg_get_token();
	if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
	goto chk_end;
      }
    }
    pg_OMP_pragma = OMP_PARALLEL;
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    goto chk_end;
  }
  
  if(PG_IS_IDENT("for")){
    pg_OMP_pragma = OMP_FOR;
    pg_get_token();
    if(pg_tok == PG_IDENT){
        if(PG_IS_IDENT("simd")){  /* for simd */
          pg_OMP_pragma = OMP_LOOP_SIMD;
          pg_get_token();
          if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
          goto chk_end;
        }
    }
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("sections")){
    pg_OMP_pragma = OMP_SECTIONS;
    pg_get_token();
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("single")){
    pg_OMP_pragma = OMP_SINGLE;
    pg_get_token();
    if((pg_OMP_list = parse_OMP_clauses()) == NULL)  goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("master")){
    pg_OMP_pragma = OMP_MASTER;
    pg_get_token();
    goto chk_end;
  }

  if(PG_IS_IDENT("critical")){
    pg_OMP_pragma = OMP_CRITICAL;
    pg_get_token();
    if(pg_tok == '('){
      if((pg_OMP_list = parse_OMP_namelist()) == NULL) goto syntax_err;
    } else pg_OMP_list = NULL;
    goto chk_end;
  }

  if(PG_IS_IDENT("ordered")){
    pg_OMP_pragma = OMP_ORDERED;
    pg_get_token();
    goto chk_end;
  }

  if(PG_IS_IDENT("section")){
    pg_OMP_pragma = OMP_SECTION;
    pg_get_token();
    ret = PRAGMA_EXEC;
    goto chk_end;
  }

  if(PG_IS_IDENT("barrier")){
    pg_OMP_pragma = OMP_BARRIER;
    ret = PRAGMA_EXEC;
    pg_get_token();
    goto chk_end;
  }
  
  if(PG_IS_IDENT("atomic")){
      pg_OMP_pragma = OMP_ATOMIC;
      ret = PRAGMA_PREFIX;
      pg_get_token();
      goto chk_end;
  }

  if(PG_IS_IDENT("flush")){
      pg_OMP_pragma = OMP_FLUSH;
      pg_get_token();
      if(pg_tok == '('){
	  if((pg_OMP_list = parse_OMP_namelist()) == NULL) goto syntax_err;
      } else pg_OMP_list = NULL;
      ret= PRAGMA_EXEC;
      goto chk_end;
  }

  if(PG_IS_IDENT("threadprivate")){
      pg_OMP_pragma = OMP_THREADPRIVATE;
      pg_get_token();
      if((pg_OMP_list = parse_OMP_namelist()) == NULL) goto syntax_err;
      ret = PRAGMA_EXEC;
      goto chk_end;
  }

  if(PG_IS_IDENT("declare")){ /* declare */
    pg_get_token();
    if(pg_tok == PG_IDENT){
        if(PG_IS_IDENT("target")){  /* declare target */
	pg_OMP_pragma = OMP_DECLARE_TARGET;
	pg_get_token();
	if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
	ret = PRAGMA_EXEC;
	goto chk_end;
      }
    }
  }

  if(PG_IS_IDENT("simd")){
    pg_OMP_pragma = OMP_SIMD;
    pg_get_token();
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("task")){
    pg_OMP_pragma = OMP_TASK;
    pg_get_token();
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("target")){  /* target */
    pg_get_token();
    if(pg_tok == PG_IDENT){
      if((ret = parse_OMP_target_pragma()) == 0) goto syntax_err;
      goto chk_end;
    }
    pg_OMP_pragma = OMP_TARGET;
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("teams")){  /* teams */
    if((ret = parse_OMP_teams_pragma()) == 0) goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("distribute")){  /* distribute */
    if((ret = parse_OMP_distribute_pragma()) == 0) goto syntax_err;
    goto chk_end;
  }
  
  if(PG_IS_IDENT("taskloop")){
    pg_OMP_pragma = OMP_TASKLOOP;
    pg_get_token();
    if(pg_tok == PG_IDENT){
        if(PG_IS_IDENT("simd")){  /* taskloop simd */
          pg_OMP_pragma = OMP_TASKLOOP_SIMD;
          pg_get_token();
          if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
          goto chk_end;
        }
    }
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    goto chk_end;
  }

  if(PG_IS_IDENT("taskwait")){
    pg_OMP_pragma = OMP_TASKWAIT;
    pg_get_token();
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    ret = PRAGMA_EXEC;
    goto chk_end;
  }

  if(PG_IS_IDENT("taskgroup")){
    pg_OMP_pragma = OMP_TASKGROUP;
    pg_get_token();
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    ret = PRAGMA_EXEC;
    goto chk_end;
  }

  if(PG_IS_IDENT("taskyield")){
    pg_OMP_pragma = OMP_TASKYIELD;
    pg_get_token();
    if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
    ret = PRAGMA_EXEC;
    goto chk_end;
  }

  addError(NULL,"OMP: unknown OMP directive, '%s'",pg_tok_buf);
 syntax_err:
    return 0;

 chk_end:
    if(pg_tok != 0) addError(NULL,"OMP:extra arguments for OMP directive");
    return ret;
}

int parse_OMP_target_pragma()
{
  int ret = PRAGMA_PREFIX; /* default */

  if(PG_IS_IDENT("data")){ /* target data */
    ret = PRAGMA_EXEC;
    pg_OMP_pragma = OMP_TARGET_DATA;
    pg_get_token();
    goto chk_end;
  }
  else if(PG_IS_IDENT("enter")){ 
    ret = PRAGMA_EXEC;
    pg_get_token();
    if(pg_tok == PG_IDENT && PG_IS_IDENT("data")){  /* target enter data */
      pg_OMP_pragma = OMP_TARGET_ENTER_DATA;
      pg_get_token();
      goto chk_end;
    }
    goto syntax_err;
  }
  else if(PG_IS_IDENT("exit")){ 
    ret = PRAGMA_EXEC;
    pg_get_token();
    if(pg_tok == PG_IDENT && PG_IS_IDENT("data")){ /* target exit data */
        pg_OMP_pragma = OMP_TARGET_EXIT_DATA;
        pg_get_token();
        goto chk_end;
    }
    goto syntax_err;
  }
  else if(PG_IS_IDENT("update")){ 
    ret = PRAGMA_EXEC;
    pg_OMP_pragma = OMP_TARGET_UPDATE;
    pg_get_token();
    goto chk_end;
  }

  if(parse_OMP_teams_pragma() == 0) goto syntax_err;

 switch(pg_OMP_pragma){
 case OMP_TEAMS:
   pg_OMP_pragma = OMP_TARGET_TEAMS;
    break;
 case OMP_TEAMS_DISTRIBUTE:
   pg_OMP_pragma = OMP_TARGET_TEAMS_DISTRIBUTE;
    break;
 case OMP_TEAMS_DISTRIBUTE_PARALLEL_LOOP:
    pg_OMP_pragma = OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_LOOP;
    break;
  case OMP_TEAMS_DISTRIBUTE_PARALLEL_LOOP_SIMD:
    pg_OMP_pragma = OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_LOOP_SIMD;
    break;
  case OMP_TEAMS_DISTRIBUTE_SIMD:
    pg_OMP_pragma = OMP_TARGET_TEAMS_DISTRIBUTE_SIMD;
    break;

  case OMP_PARALLEL:
    pg_OMP_pragma = OMP_TARGET_PARALLEL;
    break;
  case OMP_PARALLEL_LOOP:
    pg_OMP_pragma = OMP_TARGET_PARALLEL_LOOP;
    break;
  case OMP_PARALLEL_LOOP_SIMD:
    pg_OMP_pragma = OMP_TARGET_PARALLEL_LOOP_SIMD;
    break;
  case OMP_SIMD:
    pg_OMP_pragma = OMP_TARGET_SIMD;
    break;

  case OMP_NONE:
    pg_OMP_pragma = OMP_TARGET;
    break;

  case OMP_DISTRIBUTE_PARALLEL_LOOP:
  case OMP_DISTRIBUTE_PARALLEL_LOOP_SIMD:
  case OMP_DISTRIBUTE_SIMD:
  default:
    goto syntax_err;
  }
 return ret;
  
 chk_end:
  if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
  return ret;

 syntax_err:
  return 0;
}

int parse_OMP_teams_pragma()
{
  int have_teams = FALSE;
  int ret = PRAGMA_PREFIX; /* default */

  if(pg_tok == PG_IDENT && PG_IS_IDENT("teams")){ /* teams ... */
    have_teams = TRUE;
    pg_get_token();
  }

  if(parse_OMP_distribute_pragma() == 0) goto syntax_err;

  if(have_teams){
    switch(pg_OMP_pragma){
    case OMP_NONE:
      pg_OMP_pragma = OMP_TEAMS;
      break;

    case OMP_DISTRIBUTE:
      pg_OMP_pragma = OMP_TEAMS_DISTRIBUTE;
      break;
    case OMP_DISTRIBUTE_PARALLEL_LOOP:
      pg_OMP_pragma = OMP_TEAMS_DISTRIBUTE_PARALLEL_LOOP;
      break;
    case OMP_DISTRIBUTE_PARALLEL_LOOP_SIMD:
      pg_OMP_pragma = OMP_TEAMS_DISTRIBUTE_PARALLEL_LOOP_SIMD;
      break;
    case OMP_DISTRIBUTE_SIMD:
      pg_OMP_pragma = OMP_TEAMS_DISTRIBUTE_SIMD;
      break;

    case OMP_PARALLEL_LOOP:
    case OMP_PARALLEL_LOOP_SIMD:
    case OMP_SIMD:
    default:
      goto syntax_err;
    }
  }
  return ret;

 syntax_err:
   return 0;
}

int parse_OMP_distribute_pragma()
{
  int have_distribute = FALSE;
  int ret = PRAGMA_PREFIX; /* default */  

  if(pg_tok == PG_IDENT && PG_IS_IDENT("distribute")){ /* distribute ... */
    have_distribute = TRUE;
    pg_get_token();
  }

  if(parse_OMP_parallel_for_SIMD_pragma() == 0) goto syntax_err;

  if(have_distribute){
    switch(pg_OMP_pragma){
    case OMP_NONE:
      pg_OMP_pragma = OMP_DISTRIBUTE;
      break;

    case OMP_PARALLEL_LOOP:
      pg_OMP_pragma = OMP_DISTRIBUTE_PARALLEL_LOOP;
      break;
    case OMP_PARALLEL_LOOP_SIMD:
      pg_OMP_pragma = OMP_DISTRIBUTE_PARALLEL_LOOP_SIMD;
      break;
    case OMP_SIMD:
      pg_OMP_pragma = OMP_DISTRIBUTE_SIMD;
      break;

    default:
      goto syntax_err;
    }
  }
  return ret;

 syntax_err:
   return 0;
}

int parse_OMP_parallel_for_SIMD_pragma()
{
  int ret = PRAGMA_PREFIX; /* default */

  if(pg_tok == PG_IDENT){
    if(PG_IS_IDENT("parallel")){
      pg_OMP_pragma = OMP_PARALLEL;
      pg_get_token();
      if(pg_tok == PG_IDENT && PG_IS_IDENT("for")){ /* parallel for */
        pg_OMP_pragma = OMP_PARALLEL_LOOP;
        pg_get_token();
        if(pg_tok == PG_IDENT && PG_IS_IDENT("simd")){ /* parallel for simd */
          pg_OMP_pragma = OMP_PARALLEL_LOOP_SIMD;
          pg_get_token();
        }
        goto chk_end;
      } 
    } 
    else if(PG_IS_IDENT("simd")){  /* simd */
      pg_get_token();
      pg_OMP_pragma = OMP_SIMD;
      goto chk_end;
    }
  }

 chk_end:
  if((pg_OMP_list = parse_OMP_clauses()) == NULL) goto syntax_err;
  return ret;

 syntax_err:
  return 0;
}

CExpr *parse_range_expr(int clause)
{
  CExpr *list = EMPTY_LIST, *v1, *v2;

  pg_get_token();
  while(1){
    v1 = v2 = NULL;
    switch(pg_tok){
    case ')': goto err;
    case '(': goto err;
    case ':':
      v1 = (CExpr*)allocExprOfNumberConst2(0, BT_INT);
      break;
    case '*':
      v1 = (CExpr *)allocExprOfStringConst(EC_STRING_CONST, "* @{ASTERISK}@", CT_UNDEF);
      pg_get_token();
      goto next;
      break;
    default:
      v1 = pg_parse_expr();
    }

    if(pg_tok != ':'){
      if(clause == OMP_CLAUSE_DEVICE)
	v2 = (CExpr*)allocExprOfNumberConst2(1, BT_INT);
      else // (clause == OMP_CLAUSE_SHADOW)
	v2 = v1;
      goto next;
    }

    pg_get_token();
    if(pg_tok == ':')
      goto err;
    else
      v2 = pg_parse_expr();

  next:
    if (v1 == NULL && v2 == NULL)
      list = exprListAdd(list, NULL);
    else
      list = exprListAdd(list, (CExpr*)allocExprOfList2(EC_UNDEF,v1,v2));

    if(pg_tok == ')'){
      pg_get_token();
      break;
    }
    
    if(pg_tok == ',')  pg_get_token();
    else goto err;
  }

  return list;
 err:
  addError(NULL, "Syntax error in device clause");
  return NULL;
}

static CExpr* parse_layout_expr()
{
  CExpr *list = EMPTY_LIST, *v;
  pg_get_token();
  
  while(1){
    if(pg_tok == '*'){
      pg_get_token();
      v = (CExpr *)allocExprOfStringConst(EC_STRING_CONST, "* @{ASTERISK}@", CT_UNDEF);
    }
    else if(PG_IS_IDENT("block")){
      pg_get_token();
      v = (CExpr *)allocExprOfStringConst(EC_STRING_CONST, "block", CT_UNDEF);
    }
    else goto err;

    list = exprListAdd(list, v);

    if(pg_tok == ')'){
      pg_get_token();
      break;
    }
    else if(pg_tok == ','){
      pg_get_token();
      continue;
    }
    else goto err;
  }
  
  return list;

 err:
  addError(NULL, "syntax error in layout clause");
  return NULL;
}

static CExpr* parse_OMP_C_subscript_list()
{
  CExpr* list = EMPTY_LIST, *v1, *v2;

  if(pg_tok != '[') {
    addError(NULL, "parse_OMP_C_subscript_list: first token= '['");
  }
  pg_get_token();

  while(1){
    v1 = v2 = NULL;
    switch(pg_tok){
    case ']':  goto err;
    case ',':  goto err;
      break;
    case ':':
      v1 = (CExpr*)allocExprOfNumberConst2(0, BT_INT);
      break;
    default:
      v1 = pg_parse_expr();
    }

    if(pg_tok == ':') goto subarray;
    list = exprListAdd(list, v1);
    goto next;
    
  subarray:
    pg_get_token();
    if(pg_tok != ']'){
      v2 = pg_parse_expr();
    }
    list = exprListAdd(list, (CExpr*)allocExprOfList2(EC_UNDEF,v1,v2));
    
  next:
    if(pg_tok == ']'){
      pg_get_token();
    }
    else goto err;
    
    if(pg_tok != '['){
      break;
    }
    else{
      pg_get_token();
    }
  }

  return list;

 err:
  addError(NULL, "Syntax error in scripts of OpenMP directive");
  return NULL;
}

static CExpr* parse_array_list()
{
  CExpr* args = EMPTY_LIST;

  if(pg_tok != '('){
    addError(NULL,"OMP: OpenMP directive clause requires name list");
    return NULL;
  }
  pg_get_token();

 next:
  if(pg_tok != PG_IDENT){
    addError(NULL, "OpenMP: empty name list in OpenMP directive clause");
    return NULL;
  }
  else{
    if(PG_IS_IDENT("to")){
      args = exprListAdd(args, pg_parse_expr());
    }
    else if(PG_IS_IDENT("from")){
      args = exprListAdd(args, pg_parse_expr());
    }
    else if(PG_IS_IDENT("tofrom")){
      args = exprListAdd(args, pg_parse_expr());
    }
    else if(PG_IS_IDENT("alloc")){
      args = exprListAdd(args, pg_parse_expr());
    }
    else if(PG_IS_IDENT("release")){
      args = exprListAdd(args, pg_parse_expr());
    }
    else if(PG_IS_IDENT("delete")){
      args = exprListAdd(args, pg_parse_expr());
    }
    else
      goto err;
  }
  
  if(pg_tok != ':')
    goto err;
  
  pg_get_token();
  CExpr* v = pg_tok_val;
  pg_get_token();
  if(pg_tok != '['){
    args = exprListAdd(args, v);
  }
  else{
    CExpr *list     = parse_OMP_C_subscript_list();
    CExpr* arrayRef = exprBinary(EC_ARRAY_REF, v, list);
    args = exprListAdd(args, arrayRef);
  }

  if(pg_tok == ','){
    pg_get_token();
    goto next;
  }
  else if(pg_tok == ')'){
    pg_get_token();
    return args;
  }
  
 err:
  addError(NULL,"OMP: syntax error in OpenMP pragma clause");
  return NULL;
}


/*
  depend([depend-modifier,] dependency-type : locator-list)
*/
static CExpr* parse_depend_expr()
{

  CExpr* args = EMPTY_LIST;

  if(pg_tok != '('){
    addError(NULL,"OMP: OpenMP directive clause requires name list");
    return NULL;
  }
  pg_get_token();

  if(pg_tok != PG_IDENT){
    addError(NULL, "OpenMP: empty name list in OpenMP directive clause");
    return NULL;
  }

  // todo: implement depend-modifier introduced in OpenMP 5.0
  if (PG_IS_IDENT("iterator")) {
    addError(NULL, "depend-modifier in depend clause is not implemented yet");
    return NULL;
  } else {
    args = exprListAdd(args, NULL);
  }

  // in, out, inout is introduced in OpenMP 4.0
  // mutexinoutset, depobj is introduced in OpenMP 5.0
  if(PG_IS_IDENT("in")){
    args = exprListAdd(args, pg_parse_expr());

  }
  else if(PG_IS_IDENT("out")){
    args = exprListAdd(args, pg_parse_expr());
  }
  else if(PG_IS_IDENT("inout")){
    args = exprListAdd(args, pg_parse_expr());
  }
  else if(PG_IS_IDENT("mutexinoutset")){
    args = exprListAdd(args, pg_parse_expr());
  }
  else if(PG_IS_IDENT("depobj")){
    args = exprListAdd(args, pg_parse_expr());
  }
  else {
    goto err;
  }


  if(pg_tok != ':')
    goto err;

  pg_get_token();

  CExpr* v; 
  CExpr *locatorList = EMPTY_LIST;

nextLocator:
  v = pg_tok_val;
  pg_get_token();
  if(pg_tok != '['){
    // not array expression
    locatorList = exprListAdd(locatorList, v);
  }
  else{
    // array expression
    CExpr *list     = parse_OMP_C_subscript_list();
    CExpr* arrayRef = exprBinary(EC_ARRAY_REF, v, list);
    locatorList = exprListAdd(locatorList, arrayRef);
  }


  if(pg_tok == ','){
    pg_get_token();
    goto nextLocator;
  }
  else if(pg_tok == ')'){
    pg_get_token();

    args = exprListAdd(args, locatorList);
    return args;
  }
  
 err:
  addError(NULL,"OMP: syntax error in OpenMP pragma clause");
  return NULL;

}
  


static CExpr* parse_OMP_clauses()
{
  CExpr *args=EMPTY_LIST, *v, *c;
  int r = 0;

  while(pg_tok == PG_IDENT){
    if(PG_IS_IDENT("private")){
      pg_get_token();
      if((v = parse_OMP_namelist()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_DATA_PRIVATE,v);
    } else if(PG_IS_IDENT("shared")){
      pg_get_token();
      if((v = parse_OMP_namelist()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_DATA_SHARED,v);
    } else if(PG_IS_IDENT("firstprivate")){
      pg_get_token();
      if((v = parse_OMP_namelist()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_DATA_FIRSTPRIVATE,v);
    } else if(PG_IS_IDENT("lastprivate")){
      pg_get_token();
      if((v = parse_OMP_namelist()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_DATA_LASTPRIVATE,v);
    } else if(PG_IS_IDENT("copyin")){
      pg_get_token();
      if((v = parse_OMP_namelist()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_DATA_COPYIN,v);
    } else if(PG_IS_IDENT("reduction")){
      pg_get_token();
      if((v = parse_OMP_reduction_namelist(&r)) == NULL) goto syntax_err;
      c = OMP_PG_LIST(r,v);
    } else if(PG_IS_IDENT("default")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      pg_get_token();
      if(pg_tok != PG_IDENT) goto syntax_err;
      if(PG_IS_IDENT("shared")) 
	r = OMP_DEFAULT_SHARED;
      else if(PG_IS_IDENT("private")) 
	r = OMP_DEFAULT_PRIVATE;
      else if(PG_IS_IDENT("none"))
	r = OMP_DEFAULT_NONE;
      else goto syntax_err;
      pg_get_token();
      if(pg_tok != ')') goto syntax_err;
      pg_get_token();
      v = OMP_PG_LIST(r,EMPTY_LIST);
      c = OMP_PG_LIST(OMP_DATA_DEFAULT,v);
    } else if(PG_IS_IDENT("if")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      pg_get_token();

      r = OMP_NONE;
      if(parse_OMP_if_directive_name_modifier(&r) == 0) {
        goto syntax_err;
      }
      if((v = pg_parse_expr()) == NULL) goto syntax_err;
      if(pg_tok != ')') goto syntax_err;
      pg_get_token();

      if (r != OMP_NONE) {
        v = OMP_PG_LIST(r, v);
      }
      c = OMP_PG_LIST(OMP_DIR_IF, v);
    } else if(PG_IS_IDENT("schedule")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      pg_get_token();
      if(pg_tok != PG_IDENT) goto syntax_err;
      if(PG_IS_IDENT("static"))        r = (int)OMP_SCHED_STATIC;
      else if(PG_IS_IDENT("dynamic"))  r = (int)OMP_SCHED_DYNAMIC;
      else if(PG_IS_IDENT("guided"))   r = (int)OMP_SCHED_GUIDED;
      else if(PG_IS_IDENT("runtime"))  r = (int)OMP_SCHED_RUNTIME;
      else if(PG_IS_IDENT("affinity")) r = (int)OMP_SCHED_AFFINITY;
      else if(PG_IS_IDENT("auto"))     r = (int)OMP_SCHED_AUTO;
      else {
	addError(NULL,"unknown schedule method '%s'",pg_tok_buf);
      }
      pg_get_token();
      
      if(pg_tok == ','){
	pg_get_token();
	if((v = pg_parse_expr()) == NULL) goto syntax_err;
	v = OMP_PG_LIST(r,v);
      }
      else v = OMP_PG_LIST(r,NULL);
      
      if(pg_tok != ')') goto syntax_err;
      
      pg_get_token();
      c = OMP_PG_LIST(OMP_DIR_SCHEDULE,v);
    } else if(PG_IS_IDENT("ordered")){
      pg_get_token();
      c = OMP_PG_LIST(OMP_DIR_ORDERED,NULL);
    } else if(PG_IS_IDENT("nowait")){
      pg_get_token();
      c = OMP_PG_LIST(OMP_DIR_NOWAIT,NULL);
    } else if(PG_IS_IDENT("num_threads")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      pg_get_token();
      if((v = pg_parse_expr()) == NULL) goto syntax_err;
      if(pg_tok != ')') goto syntax_err;
      pg_get_token();
      c = OMP_PG_LIST(OMP_DIR_NUM_THREADS,v);
    } else if(PG_IS_IDENT("collapse")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      pg_get_token();
      if((v = pg_parse_expr()) == NULL) goto syntax_err;
      if(pg_tok != ')') goto syntax_err;
      pg_get_token();
      c = OMP_PG_LIST(OMP_COLLAPSE,v);
    }
    else if(PG_IS_IDENT("map")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      if((v = parse_array_list()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_TARGET_DATA_MAP, v);
    } else if(PG_IS_IDENT("to")){
      pg_get_token();
      if((v = parse_OMP_namelist()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_DECLARE_TARGET_TO,v);
    } else if(PG_IS_IDENT("device")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      if((v = parse_range_expr(OMP_CLAUSE_DEVICE)) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_TARGET_DEVICE,v);
    } else if(PG_IS_IDENT("shadow")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      if((v = parse_range_expr(OMP_CLAUSE_SHADOW)) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_TARGET_SHADOW,v);
    } else if(PG_IS_IDENT("layout")){
      pg_get_token();
      if(pg_tok != '(') goto syntax_err;
      if((v = parse_layout_expr()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_TARGET_LAYOUT,v);
    } else if (PG_IS_IDENT("depend")) {
      pg_get_token();
      if (pg_tok != '(') goto syntax_err;
      if((v = parse_depend_expr()) == NULL) goto syntax_err;
      c = OMP_PG_LIST(OMP_DEPEND, v);
    } else if (PG_IS_IDENT("final")) {
      pg_get_token();
      if (pg_tok != '(') goto syntax_err;
      pg_get_token();
      if ((v = pg_parse_expr()) == NULL) goto syntax_err;
      if (pg_tok != ')') goto syntax_err;
      pg_get_token();
      c = OMP_PG_LIST(OMP_FINAL, v);
    } else if (PG_IS_IDENT("priority")) {
      pg_get_token();
      if (pg_tok != '(') goto syntax_err;
      pg_get_token();
      if ((v = pg_parse_expr()) == NULL) goto syntax_err;
      if (pg_tok != ')') goto syntax_err;
      pg_get_token();
      c = OMP_PG_LIST(OMP_PRIORITY,v);
    } else if (PG_IS_IDENT("untied")) {
      pg_get_token();
      c = OMP_PG_LIST(OMP_UNTIED, NULL);
    } else if (PG_IS_IDENT("mergeable")) {
      pg_get_token();
      c = OMP_PG_LIST(OMP_MERGEABLE, NULL);
    } else if (PG_IS_IDENT("grainsize")) {
      pg_get_token();
      if (pg_tok != '(') goto syntax_err;
      pg_get_token();
      if ((v = pg_parse_expr()) == NULL) goto syntax_err;
      if (pg_tok != ')') goto syntax_err;
      pg_get_token();
      c = OMP_PG_LIST(OMP_GRAINSIZE,v);
    } else if (PG_IS_IDENT("num_tasks")) {
      pg_get_token();
      if (pg_tok != '(') goto syntax_err;
      pg_get_token();
      if ((v = pg_parse_expr()) == NULL) goto syntax_err;
      if (pg_tok != ')') goto syntax_err;
      pg_get_token();
      c = OMP_PG_LIST(OMP_NUM_TASKS,v);
    } else if (PG_IS_IDENT("nogroup")) {
      pg_get_token();
      c = OMP_PG_LIST(OMP_NOGROUP, NULL);
    }
    else {
      addError(NULL,"unknown OMP directive clause '%s'", pg_tok_buf);
      goto syntax_err;
    }
    args = exprListAdd(args, c);
  }

  return args;
    
 syntax_err:
    addError(NULL,"OMP: syntax error in OMP pragma clause");
    return NULL;
}

static void get_peek_token(char *head, char **token,
                           size_t *token_len, char **next,
                           int *num_peek)
{
  (*num_peek)++;
  *token_len = 0;
  pg_get_peek_token(head, token, token_len, next);
}

static int parse_OMP_if_directive_name_modifier(int *r)
{
  char *next = pg_cp;
  char *token = NULL;
  size_t token_len = 0;
  int num_peek = 0;
  int modifier = OMP_NONE;

  if (PG_IS_IDENT("task")) {
    get_peek_token(next, &token, &token_len, &next, &num_peek);
    modifier = OMP_TASK;
  } else if (PG_IS_IDENT("taskloop")) {
    get_peek_token(next, &token, &token_len, &next, &num_peek);
    modifier = OMP_TASKLOOP;
  } else if (PG_IS_IDENT("target")) {
    get_peek_token(next, &token, &token_len, &next, &num_peek);
    modifier = OMP_TARGET;

    if (token != NULL && *token != '\0') {
      if(strncmp(token, "update", token_len) == 0) {
        get_peek_token(next, &token, &token_len, &next, &num_peek);
        modifier = OMP_TARGET_UPDATE;
      } else if(strncmp(token, "data", token_len) == 0) {
        get_peek_token(next, &token, &token_len, &next, &num_peek);
        modifier = OMP_TARGET_DATA;
      } else if(strncmp(token, "enter", token_len) == 0) {
        get_peek_token(next, &token, &token_len, &next, &num_peek);

        if (token != NULL && *token != '\0') {
          if(strncmp(token, "data", token_len) == 0) {
            get_peek_token(next, &token, &token_len, &next, &num_peek);
            modifier = OMP_TARGET_ENTER_DATA;
          }
        } else {
          goto syntax_err;
        }
      } else if(strncmp(token, "exit", token_len) == 0) {
        get_peek_token(next, &token, &token_len, &next, &num_peek);

        if (token != NULL && *token != '\0') {
          if(strncmp(token, "data", token_len) == 0) {
            get_peek_token(next, &token, &token_len, &next, &num_peek);
            modifier = OMP_TARGET_EXIT_DATA;
          }
        } else {
          goto syntax_err;
        }
      }
    } else {
      goto syntax_err;
    }
  } else if (PG_IS_IDENT("parallel")) {
    get_peek_token(next, &token, &token_len, &next, &num_peek);
    modifier = OMP_PARALLEL_FOR;
  }

  if (modifier != OMP_NONE) {
    if (token == NULL || *token == '\0') {
      goto syntax_err;
    }

    if (*token == ':') {
      *r = modifier;

      // Next token. Skip tokens for peek.
      pg_get_seek_token(num_peek + 1);
    }
  }

  return 1;

 syntax_err:
  addError(NULL,"OMP if clause requires modifier or expression.");
  return 0;
}


static CExpr* parse_OMP_namelist()
{
    CExpr* args = EMPTY_LIST;
    if(pg_tok != '(') {
      addError(NULL,"OMP: OMP directive clause requires name list");
      return NULL;
    }
    pg_get_token();
    
 next:
    if(pg_tok != PG_IDENT){
      addError(NULL,"OMP: empty name list in OMP directive clause");
	return NULL;
    }

    args = exprListAdd(args, pg_tok_val);
    pg_get_token();
    if(pg_tok == ','){
      pg_get_token();
      goto next;
    }
    else if(pg_tok == ')'){
      pg_get_token();
      return args;
    } 

    addError(NULL,"OMP: syntax error in OMP pragma clause");
    return NULL;
}

static CExpr* parse_OMP_reduction_namelist(int *r)
{
  CExpr* args = EMPTY_LIST;
    if(pg_tok != '('){
      addError(NULL,"OMP reduction clause requires name list");
	return NULL;
    }
    pg_get_token();
    switch(pg_tok){
    case '+': *r = OMP_DATA_REDUCTION_PLUS;         break;
    case '-': *r = OMP_DATA_REDUCTION_MINUS;        break;
    case '*': *r = OMP_DATA_REDUCTION_MUL;          break;
    case '&': *r = OMP_DATA_REDUCTION_BITAND;       break;
    case '|': *r = OMP_DATA_REDUCTION_BITOR;        break;
    case '^': *r = OMP_DATA_REDUCTION_BITXOR;       break;
    case PG_ANDAND: *r = OMP_DATA_REDUCTION_LOGAND; break;
    case PG_OROR:   *r = OMP_DATA_REDUCTION_LOGOR;  break;
    case PG_IDENT:
      if(PG_IS_IDENT("max")) { *r = OMP_DATA_REDUCTION_MAX; break; }
      if(PG_IS_IDENT("min")) { *r = OMP_DATA_REDUCTION_MIN; break; }
    default:
      return NULL;	/* syntax error */
    }
    pg_get_token();
    if(pg_tok != ':') return NULL;
    pg_get_token();

 next:
    if(pg_tok != PG_IDENT){
      addError(NULL,"empty name list in OMP reduction clause");
	return NULL;
    }
    args = exprListAdd(args,pg_tok_val);
    pg_get_token();
    if(pg_tok == ','){
	pg_get_token();
	goto next;
    } else if(pg_tok == ')'){
	pg_get_token();
	return args;
    } 

    addError(NULL,"syntax error in OMP directive clause");
    return NULL;
}

#ifdef not
/*
 * compile pragma, called from compile_statement 
 */

expv compile_OMP_pragma(enum OMP_pragma pragma,expr x)
{
    expv v,c;
    expv pclause,dclause;

    switch(pragma){
    case OMP_PARALLEL: 		/* parallel <clause_list> */
	compile_OMP_pragma_clause(EXPR_ARG2(x),OMP_PARALLEL,TRUE,
				  &pclause,&dclause);
	v = compile_statement(EXPR_ARG3(x));
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),pclause,v);

    case OMP_PARALLEL_FOR:	/* parallel for <clause_list> */
	compile_OMP_pragma_clause(EXPR_ARG2(x),OMP_FOR,TRUE,
				  &pclause,&dclause);
	v = compile_statement(EXPR_ARG3(x));
	return elist3(EXPR_LINE(x),OMP_PRAGMA,
		      make_enode(INT_CONSTANT, (void *)OMP_PARALLEL), pclause,
		      elist3(EXPR_LINE(x),OMP_PRAGMA,
			     make_enode(INT_CONSTANT, (void *)OMP_FOR),
			     dclause,v));

    case OMP_PARALLEL_SECTIONS: /* parallel sections <clause_list> */
	compile_OMP_pragma_clause(EXPR_ARG2(x),OMP_SECTIONS,TRUE,
				  &pclause,&dclause);
	v = compile_OMP_SECTIONS_statement(EXPR_ARG3(x));
	return elist3(EXPR_LINE(x),OMP_PRAGMA,
		      make_enode(INT_CONSTANT, (void *)OMP_PARALLEL), pclause,
		      elist3(EXPR_LINE(x),OMP_PRAGMA,
			     make_enode(INT_CONSTANT, (void *)OMP_SECTIONS),
			     dclause, v));

    case OMP_FOR:		/* for <clause_list> */
	compile_OMP_pragma_clause(EXPR_ARG2(x),OMP_FOR,FALSE,
				  &pclause,&dclause);
	v = compile_statement(EXPR_ARG3(x));
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),dclause,v);
		     
    case OMP_SECTIONS:		/* sections <clause_list> */
	compile_OMP_pragma_clause(EXPR_ARG2(x),OMP_SECTIONS,FALSE,
				  &pclause,&dclause);
	if((v = compile_OMP_SECTIONS_statement(EXPR_ARG3(x))) == NULL)
	  break;
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),dclause,v);

    case OMP_SINGLE:		/* single <clause list> */
	compile_OMP_pragma_clause(EXPR_ARG2(x),OMP_SINGLE,FALSE,
				  &pclause,&dclause);
	v = compile_statement(EXPR_ARG3(x));
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),dclause,v);

    case OMP_MASTER:		/* master */
    case OMP_ORDERED:		/* ordered */
	v = compile_statement(EXPR_ARG3(x));
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),NULL,v);

    case OMP_CRITICAL:		/* critical <name> */
	v = compile_statement(EXPR_ARG3(x));
	c = EXPR_ARG2(x);
	if(c != NULL && LIST_NEXT(EXPR_LIST(c)) != NULL){
	    error_at_node(x,"bad critical section name");
	    break;
	}
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),c,v);

    case OMP_ATOMIC:		/* atomic */
	/* should check next statment */
	if((v = compile_statement(EXPR_ARG3(x))) == NULL) 
	  break;
	if(EXPV_CODE(v) != EXPR_STATEMENT){
	    error_at_node(x,"bad statement for OMP atomic directive");
	    break;
	}
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),NULL,v);

    case OMP_FLUSH:		/* flush <namelist> */
	c = EXPR_ARG2(x);
	compile_OMP_name_list(c);
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),c,NULL);

    case OMP_SECTION:		/* section */
	/* section directive must appear in section block */
	error_at_node(x,"'section' directive in SECTIONS");
	break;

    case OMP_BARRIER:		/* barrier */
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),NULL,NULL);

    case OMP_THREADPRIVATE:
	c = EXPR_ARG2(x);
	compile_OMP_name_list(c);
	return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),c,NULL);

    case OMP_DECLARE_TARGET:
      c = EXPR_ARG2(x);
      compile_OMP_name_list(c);
      return elist3(EXPR_LINE(x),OMP_PRAGMA,EXPR_ARG1(x),c,NULL);
      
    default:
	fatal("compile_pragma_line: unknown pragma %d",pragma);
    }
    return NULL;
}

static expv compile_OMP_SECTIONS_statement(expr x)
{
    expr xx;
    expv section_list,current_section;
    list lp;

    if(EXPR_CODE(x) != COMPOUND_STATEMENT){
	error_at_node(x,"sections directive must be followed by compound statement block");
	return NULL;
    }
    xx = EXPR_ARG1(x);
    if(xx != NULL){
	error_at_node(xx,"declarations in sections block");
	return NULL;
    }
    section_list = EMPTY_LIST;
    current_section = NULL;
    FOR_ITEMS_IN_LIST(lp,EXPR_ARG2(x)){
	xx = LIST_ITEM(lp);
	if(EXPR_CODE(xx) == PRAGMA_LINE &&
	   EXPR_INT(EXPR_ARG1(xx)) == OMP_SECTION){
	    if(current_section != NULL){
		current_section = list3(COMPOUND_STATEMENT,
					list0(ID_LIST),list0(LIST),
					current_section);
		section_list = exprListAdd(section_list,current_section);
	    }
	    current_section = EMPTY_LIST;
	    continue;
	}
	if(current_section == NULL){
	    /* error_at_node(xx,"statement is not in any section");
	    return NULL; */
	    current_section = EMPTY_LIST;
	}
	current_section = exprListAdd(current_section,
					compile_statement(xx));
    }
    current_section = list3(COMPOUND_STATEMENT,
			    list0(ID_LIST),list0(LIST),
			    current_section);
    section_list = exprListAdd(section_list,current_section);
    return section_list;
}

/* PARALLEL - private,firstprivate,reduction,default,shared,copyin,if
 * FOR      - private,firstprivate,lastprivate,reduction,ordered,shed,nowait
 * SECTIONS - private,firstprivate,lastprivate,reduction,nowait
 * SINGLE   - private,firstprivate,nowait
 */
static void compile_OMP_pragma_clause(expr x, int pragma, int is_parallel,
				      expv *pc,expv *dc)
{
    list lp;
    expr c,v;
    expv pclause = NULL;
    expv dclause;

    if(is_parallel) pclause = EMPTY_LIST;
    dclause = EMPTY_LIST;
    FOR_ITEMS_IN_LIST(lp,x){
      c = LIST_ITEM(lp);
      switch(EXPR_INT(EXPR_ARG1(c))){
      case OMP_DATA_DEFAULT:	/* default(shared|none) */
	if(!is_parallel){
	  error_at_node(x,"'default' clause must be in PARALLEL");
	  break;
	}
	pclause = exprListAdd(pclause,c);
	break;
      case OMP_DATA_SHARED:
	compile_OMP_name_list(EXPR_ARG2(c));
	if(!is_parallel){
	  error_at_node(x,"'shared' clause must be in PARALLEL");
	  break;
	}
	pclause = exprListAdd(pclause,c);
	break;
      case OMP_DATA_COPYIN:
	compile_OMP_name_list(EXPR_ARG2(c));
	if(!is_parallel){
	  error_at_node(x,"'copyin' clause must be in PARALLEL");
	  break;
	}
	pclause = exprListAdd(pclause,c);
	break;
      case OMP_DIR_IF:
	if(!is_parallel){
	  error_at_node(x,"'if' clause must be in PARALLEL");
	  break;
	}
	v = compile_expression(EXPR_ARG2(c));
	pclause = exprListAdd(pclause, list2(LIST,EXPR_ARG1(c),v));
	break;
      case OMP_DATA_PRIVATE:
      case OMP_DATA_FIRSTPRIVATE:
	/* all pragma can have these */
	compile_OMP_name_list(EXPR_ARG2(c));
	if(pragma == OMP_PARALLEL)
	  pclause = exprListAdd(pclause,c);
	else     
	  dclause = exprListAdd(dclause,c);
	break;
	
      case OMP_DATA_LASTPRIVATE:
	compile_OMP_name_list(EXPR_ARG2(c));
	if(pragma != OMP_FOR && pragma != OMP_SECTIONS){
	  error_at_node(x,"'lastprivate' clause must be in FOR or SECTIONS");
	  break;
	}
	dclause = exprListAdd(dclause,c);
	break;
	
      case OMP_DATA_REDUCTION_PLUS:
      case OMP_DATA_REDUCTION_MINUS:
      case OMP_DATA_REDUCTION_MUL:
      case OMP_DATA_REDUCTION_BITAND:
      case OMP_DATA_REDUCTION_BITOR:
      case OMP_DATA_REDUCTION_BITXOR:
      case OMP_DATA_REDUCTION_LOGAND:
      case OMP_DATA_REDUCTION_LOGOR:
      case OMP_DATA_REDUCTION_MIN:
      case OMP_DATA_REDUCTION_MAX:
	compile_OMP_name_list(EXPR_ARG2(c));
	if(pragma == OMP_PARALLEL)
	  pclause = exprListAdd(pclause,c);
	else if(pragma == OMP_FOR || pragma == OMP_SECTIONS)
	  dclause = exprListAdd(dclause,c);
	else 
	  error_at_node(x,"'reduction' clause must not be in SINGLE");
	break;
	
      case OMP_DIR_ORDERED:
	if(pragma != OMP_FOR){
	  error_at_node(x,"'ordered' clause must be in FOR");
	  break;
	}
	dclause = exprListAdd(dclause,c);
	break;
	
      case OMP_DIR_SCHEDULE:
	if(pragma != OMP_FOR){
	  error_at_node(x,"'schedule' clause must be in FOR");
	  break;
	}
	v = EXPR_ARG2(EXPR_ARG2(c));
	if(v != NULL && 
	   EXPR_INT(EXPR_ARG1(EXPR_ARG2(c))) != (int)OMP_SCHED_AFFINITY){
	  v = compile_expression(v);
	  c = list2(LIST,EXPR_ARG1(c),
		    list2(LIST,EXPR_ARG1(EXPR_ARG2(c)),v));
	}
	dclause = exprListAdd(dclause,c);
	break;
	
      case OMP_DIR_NOWAIT:
	if(is_parallel){
	  error_at_node(x,"'nowait' clause must not be in PARALLEL");
	  break;
	}
	dclause = exprListAdd(dclause,c);
	break;
	
      default:
	fatal("compile_OMP_paragma_clause");
      }
    }

    /* combination with PARALLEL, don't have to wait */
    if(is_parallel && (pragma != OMP_PARALLEL))
      dclause = exprListAdd(dclause, OMP_PG_LIST(OMP_DIR_NOWAIT, NULL));

    *pc = pclause;
    *dc = dclause;
}

static CExpr* compile_OMP_name_list(expr x)
{
    list lp;
    expr v;
    ID id;
    TYPE_DESC tp;

    FOR_ITEMS_IN_LIST(lp,x){
	v = LIST_ITEM(lp);
	id = lookup_ident(v);
	if(id == NULL){
	    error_at_node(x, "undefined variable, %s in pragma", 
			  SYM_NAME(EXPR_SYM(v)));
	    continue;
	}
	switch(ID_CLASS(id)){
	case AUTO:	/* auto variable */
	case PARAM:	/* paramter */
	case EXTERN:	/* extern variable */
	case EXTDEF:	/* external defition */
	case STATIC:	/* static variable */
	case REGISTER:	/* register variable */
	    tp = ID_TYPE(id);
	    if ( IS_FUNCTION(tp) ){
		error_at_node(x, "function name, %s in pragma", 
			      SYM_NAME(EXPR_SYM(v)));
	    }
	    break;
	default:
	  error_at_node(x, "identifer, %s is not variable in pragma",
			SYM_NAME(EXPR_SYM(v)));
	}
    }
}

#endif
