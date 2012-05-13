#include "evpath.h"
#include <stdio.h>
#define MON_PORT 5353
#define MON_HOST "localhost"

typedef struct _simple_rec {
    int integer_field;
} simple_rec, *simple_rec_ptr;

static FMField simple_field_list[] =
{
    {"integer_field", "integer", sizeof(int), FMOffset(simple_rec_ptr, integer_field)},
    {NULL, NULL, 0, 0}
};
static FMStructDescRec simple_format_list[] =
{
    {"simple", simple_field_list, sizeof(simple_rec), NULL},
    {NULL, NULL}
};

/* this file is evpath/examples/net_send.c */
int main(int argc, char **argv)
{
    CManager cm;
    simple_rec data;
    EVstone stone;
    EVsource source;
    char string_list[2048];
    attr_list contact_list;
    EVstone remote_stone = 0 /* assume we only have one stone on the central */;

    cm = CManager_create();
    CMlisten(cm);

    stone = EValloc_stone(cm);
    contact_list = create_attr_list();
    add_int_attr(contact_list, attr_atom_from_string("IP_PORT"), MON_PORT);
    add_string_attr(contact_list, attr_atom_from_string("IP_HOST"), MON_HOST);
    EVassoc_bridge_action(cm, stone, contact_list, remote_stone);

    source = EVcreate_submit_handle(cm, stone, simple_format_list);
    data.integer_field = 318;
    EVsubmit(source, &data, NULL);
}
