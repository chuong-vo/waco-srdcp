#include <stdbool.h>
#include <stdio.h>
#include "my_collect.h"
#include <string.h>
#include <stdint.h>

// -------------------------------------------------------------------------------------------------
//                                      DICT IMPLEMENTATION
// -------------------------------------------------------------------------------------------------

void print_dict_state(TreeDict *dict)
{
        int i;
        for (i = 0; i < dict->len; i++)
        {
                printf("\tDictEntry %d: node %02x:%02x - parent %02x:%02x\n",
                       i,
                       dict->entries[i].key.u8[0],
                       dict->entries[i].key.u8[1],
                       dict->entries[i].value.u8[0],
                       dict->entries[i].value.u8[1]);
        }
}

int dict_find_index(TreeDict *dict, const linkaddr_t key)
{
        int i;
        for (i = 0; i < dict->len; i++)
        {
                DictEntry tmp = dict->entries[i];
                if (linkaddr_cmp(&(tmp.key), &key) != 0)
                {
                        return i;
                }
        }
        return -1;
}

linkaddr_t dict_find(TreeDict *dict, const linkaddr_t *key)
{
        int idx = dict_find_index(dict, *key);
        linkaddr_t ret;
        if (idx == -1)
        {
                linkaddr_copy(&ret, &linkaddr_null);
        }
        else
        {
                linkaddr_copy(&ret, &dict->entries[idx].value);
        }
        return ret;
}

int dict_add(TreeDict *dict, const linkaddr_t key, linkaddr_t value)
{
        /*
           Adds a new entry to the Dictionary
           In case the key already exists, it replaces the value
         */
        printf("Dictionary add: key: %02x:%02x value: %02x:%02x\n",
               key.u8[0], key.u8[1], value.u8[0], value.u8[1]);
        int idx = dict_find_index(dict, key);
        if (idx != -1)
        { // Element already present, update its value
                linkaddr_copy(&dict->entries[idx].value, &value);
                return 0;
        }
        // Try to insert new element
        if (dict->len == MAX_NODES)
        {
                printf("Dictionary is full. MAX_NODES cap reached. Proposed key: %02x:%02x value: %02x:%02x\n",
                       key.u8[0], key.u8[1], value.u8[0], value.u8[1]);
                return -1;
        }
        linkaddr_copy(&dict->entries[dict->len].key, &key);
        linkaddr_copy(&dict->entries[dict->len].value, &value);
        dict->len++;
        return 0;
}

// -------------------------------------------------------------------------------------------------
//                                      ROUTING TABLE MANAGEMENT
// -------------------------------------------------------------------------------------------------

/*
    Initialize the path by setting each entry to linkaddr_null
 */
void init_routing_path(my_collect_conn *conn)
{
        int i = 0;
        linkaddr_t *path_ptr = conn->routing_table.tree_path;
        while (i < MAX_PATH_LENGTH)
        {
                linkaddr_copy(path_ptr, &linkaddr_null);
                path_ptr++;
                i++;
        }
}

/*
    Check if target is already in the path.
    len: number of linkaddr_t elements already present in the path.
 */
int already_in_route(my_collect_conn *conn, uint8_t len, linkaddr_t *target)
{
        int i;
        for (i = 0; i < len; i++)
        {
                if (linkaddr_cmp(&conn->routing_table.tree_path[i], target))
                {
                        return true;
                }
        }
        return false;
}

/*
    Search for a path from the sink to the destination node, going backwards
    from the destiantion throught the parents. If not proper path is found returns 0,
    otherwise the path length.
    The linkddr_t addresses of the nodes in the path are written to the tree_path
    array in the conn object.
 */
int find_route(my_collect_conn *conn, const linkaddr_t *dest)
{
        init_routing_path(conn);

        uint8_t path_len = 0;
        linkaddr_t parent;
        linkaddr_copy(&parent, dest);
        do
        {
                // copy into path the fist entry (dest node)
                memcpy(&conn->routing_table.tree_path[path_len], &parent, sizeof(linkaddr_t));
                parent = dict_find(&conn->routing_table, &parent);
                // abort in case a node has no parent or the path presents a loop
                if (linkaddr_cmp(&parent, &linkaddr_null) ||
                    already_in_route(conn, path_len, &parent))
                {
                        printf("PATH ERROR: cannot build path for destination node: %02x:%02x. Loop detected.\n",
                               (*dest).u8[0], (*dest).u8[1]);
                        return 0;
                }
                path_len++;
        } while (!linkaddr_cmp(&parent, &sink_addr) && path_len < MAX_PATH_LENGTH);

        if (path_len > MAX_PATH_LENGTH)
        {
                // path too long
                printf("PATH ERROR: Path too long for destination node: %02x:%02x\n",
                       (*dest).u8[0], (*dest).u8[1]);
                return 0;
        }
        return path_len;
}

void print_route(my_collect_conn *conn, uint8_t route_len, const linkaddr_t *dest)
{
        uint8_t i;
        printf("Sink route to node %02x:%02x:\n", (*dest).u8[0], (*dest).u8[1]);
        for (i = 0; i < route_len; i++)
        {
                printf("\t%d: %02x:%02x\n",
                       i,
                       conn->routing_table.tree_path[i].u8[0],
                       conn->routing_table.tree_path[i].u8[1]);
        }
}
