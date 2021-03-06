/**
 * \file F-ident.c
 */

#include "F-front.h"

ID new_ident_desc(SYMBOL sp)
{
    ID id;
    id = XMALLOC(ID, sizeof(*id));
    ID_SYM(id) = sp;
    return (id);
}

EXT_ID
new_external_id(SYMBOL sp)
{
    EXT_ID id;
    id = XMALLOC(EXT_ID, sizeof(*id));
    EXT_SYM(id) = sp;
    return (id);
}

EXT_ID
new_external_id_for_external_decl(SYMBOL sp, TYPE_DESC tp)
{
    EXT_ID id = new_external_id(sp);
    EXT_TAG(id) = STG_EXT;
    EXT_PROC_TYPE(id) = tp;
    EXT_IS_OFMODULE(id) = FALSE;
    return id;
}

ID find_ident_head(SYMBOL s, ID head)
{
    ID ip;
    FOREACH_ID (ip, head) {
        if (ID_SYM(ip) == s)
            return ip;
    }
    return NULL;
}

EXT_ID
find_ext_id_head(SYMBOL s, EXT_ID head)
{
    EXT_ID ep;
    FOREACH_EXT_ID (ep, head) {
        if (EXT_SYM(ep) && strcmp(SYM_NAME(EXT_SYM(ep)), SYM_NAME(s)) == 0)
            return ep;
    }
    return NULL;
}

void extid_put_last(EXT_ID base, EXT_ID to_add)
{
    EXT_ID ep, last_ep = NULL;

    assert(base != NULL);
    for (ep = base; ep != NULL; ep = EXT_NEXT(ep)) {
        last_ep = ep;
    }
    assert(last_ep != NULL);
    EXT_NEXT(last_ep) = to_add;
}

void declare_storage(ID id, enum storage_class stg)
{
    if (ID_STORAGE(id) == STG_UNKNOWN)
        ID_STORAGE(id) = stg;
    else if (ID_STORAGE(id) != stg)
        error("incompatible storage declarations, %s", ID_NAME(id));
}

void id_multilize(ID id)
{
    ID new_child;

    new_child = new_ident_desc(ID_SYM(id));
    *new_child = *id;
    ID_NEXT(new_child) = NULL;

    MULTI_ID_LIST(id) = new_child;
    ID_CLASS(id) = CL_MULTI;
}

ID multi_find_class(const ID id, const enum name_class cls)
{
    ID ip;

    if (ID_CLASS(id) != CL_MULTI) {
        return NULL;
    }

    FOREACH_ID (ip, MULTI_ID_LIST(id)) {
        if (ID_CLASS(ip) == cls) {
            return ip;
        }
    }

    FOREACH_ID (ip, ID_NEXT(id)) {
        if (ID_SYM(id) == ID_SYM(ip) && ID_CLASS(ip) == cls) {
            return ip;
        }
    }

    return NULL;
}

int id_link_remove(ID *head, ID tobeRemoved)
{
    ID ip, pre = NULL;
    if (head == NULL)
        return FALSE;

    FOREACH_ID (ip, *head) {
        if (ID_SYM(ip) == ID_SYM(tobeRemoved)) {
            if (pre == NULL) {
                *head = ID_NEXT(ip);
            } else {
                ID_NEXT(pre) = ID_NEXT(ip);
            }
            return TRUE;
        }
        pre = ip;
    }
    return FALSE;
}
