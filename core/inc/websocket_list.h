#ifndef __WEBSOCKET_LIST_H__
#define __WEBSOCKET_LIST_H__

typedef struct ws_list
{
    struct ws_list *next;
    struct ws_list *prev;
}ws_list_t;

#define WS_LIST_OBJECT_INIT(object)    {&(object), &(object)}

#define ws_container_of(node, type, membe)  \
    ((type *)((char *)node - (size_t)&(((type *)(0))->membe)))

#define ws_list_entry(node, type, member) \
    ws_container_of(node, type, member)

#define ws_list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define ws_list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
        pos = n, n = pos->next)

static __inline void ws_list_init(ws_list_t *l)
{
    l->next = l->prev = l;
}

static __inline void ws_list_insert_after(ws_list_t *l, ws_list_t *n)
{
    l->next->prev = n;
    n->prev = l;
    n->next = l->next;
    l->next = n;
}

static __inline void ws_list_insert_before(ws_list_t *l, ws_list_t *n)
{
    l->prev->next = n;
    n->prev = l->prev;
    n->next = l;
    l->prev = n;
}

static __inline void ws_list_remove(ws_list_t *n)
{
    n->next->prev = n->prev;
    n->prev->next = n->next;
    n->next = n->prev = n;
}

#endif //__WEBSOCKET_LIST_H__