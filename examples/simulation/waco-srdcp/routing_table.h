#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

// ------------------------------------------------------------
//                DICT IMPLEMENTATION
// ------------------------------------------------------------


void print_dict_state(TreeDict*);
int dict_find_index(TreeDict*, const linkaddr_t);
int dict_add(TreeDict*, const linkaddr_t, linkaddr_t);

// ------------------------------------------------------------
//                ROUTING TABLE MANAGEMENT
// ------------------------------------------------------------

void init_routing_path(my_collect_conn*);
int already_in_route(my_collect_conn*, uint8_t, linkaddr_t*);
int find_route(my_collect_conn*, const linkaddr_t*);
void print_route(my_collect_conn*, uint8_t, const linkaddr_t*);

#endif //ROUTING_TABLE_H
