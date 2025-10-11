#include <stdbool.h>
#include <stdio.h>
#include "my_collect.h"
#include <string.h>
#include <stdint.h>

// -------------------------------------------------------------------------------------------------
//                                      DICT IMPLEMENTATION
// -------------------------------------------------------------------------------------------------

/**
 * @brief Prints the entire content of the parent table (dictionary) at the SINK.
 * @param dict The parent table to print.
 */
void print_dict_state(TreeDict *dict)
{
        int i;
        for (i = 0; i < dict->len; i++)
        {
                printf("\tDictEntry %d: node %02u:%02u - parent %02u:%02u\n",
                       i,
                       dict->entries[i].key.u8[0],
                       dict->entries[i].key.u8[1],
                       dict->entries[i].value.u8[0],
                       dict->entries[i].value.u8[1]);
        }
}

/**
 * @brief Finds the index of an entry by its key (node address) in the dictionary.
 * @param dict The parent table.
 * @param key  The node address.
 * @return The index of the entry, or -1 if not found.
 */
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

/**
 * @brief Looks up the parent of a node in the dictionary.
 * @param dict The parent table.
 * @param key  A pointer to the node address to look up.
 * @return The parent's address, or linkaddr_null if not found.
 */
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

/* ---- PATCH START (routing_table.c: dict_add) ---- */
/**
 * @brief Adds or updates a (node -> parent) entry in the dictionary.
 * @param dict  The parent table.
 * @param key   The node's address.
 * @param value The parent's address.
 * @return 0 on success, or -1 if the dictionary is full.
 * @details Normalizes the high byte of addresses, ignores null entries, and
 *          replaces the value if the key already exists.
 */
int dict_add(TreeDict *dict, const linkaddr_t key, linkaddr_t value)
{
        /* Normalize: high byte = 0 to avoid garbage/endianness issues */
        linkaddr_t k = key;
        linkaddr_t v = value;
        k.u8[1] = 0x00;
        v.u8[1] = 0x00;
        /* Discard null entries (node 00 or parent 00) */
        if (k.u8[0] == 0 || v.u8[0] == 0)
        {
                /* printf("Dictionary drop: key %02u:%02u value %02u:%02u\n", k.u8[0], k.u8[1], v.u8[0], v.u8[1]); */
                return 0;
        }
        printf("Dictionary add: key: %02u:%02u value: %02u:%02u\n",
               k.u8[0], k.u8[1], v.u8[0], v.u8[1]);

        int idx = dict_find_index(dict, k);
        if (idx != -1) /* Update existing entry */
        {
                linkaddr_copy(&dict->entries[idx].value, &v);
                return 0;
        } /* Insert new entry */
        if (dict->len == MAX_NODES)
        {
                printf("Dictionary is full. MAX_NODES cap reached. Proposed key: %02u:%02u value: %02u:%02u\n",
                       k.u8[0], k.u8[1], v.u8[0], v.u8[1]);
                return -1;
        }
        linkaddr_copy(&dict->entries[dict->len].key, &k);
        linkaddr_copy(&dict->entries[dict->len].value, &v);
        dict->len++;
        return 0;
}
/* ---- PATCH END ---- */

// -------------------------------------------------------------------------------------------------
//                                      ROUTING TABLE MANAGEMENT
// -------------------------------------------------------------------------------------------------

/**
 * @brief Initializes the routing path array (tree_path) with linkaddr_null.
 * @param conn The collect connection structure (which contains routing_table.tree_path).
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

/**
 * @brief Checks if a target address is already in the path (loop detection).
 * @param conn   The collect connection structure (containing tree_path).
 * @param len    The current number of elements in the path.
 * @param target A pointer to the address to check.
 * @return true if the address is already in the path, false otherwise.
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

/**
 * @brief Finds a route from the SINK to a destination by backtracking through parents.
 * @param conn The collect connection structure (at the SINK).
 * @param dest The destination address.
 * @return The path length, or 0 on error (no route, loop detected, or path too long).
 * @details The path is constructed by traversing the parent dictionary backwards from the destination.
 */
int find_route(my_collect_conn *conn, const linkaddr_t *dest)
{
        init_routing_path(conn);

        uint8_t path_len = 0;
        linkaddr_t parent;
        linkaddr_copy(&parent, dest);
        do
        {
                /* Copy the current node into the path */
                memcpy(&conn->routing_table.tree_path[path_len], &parent, sizeof(linkaddr_t));
                parent = dict_find(&conn->routing_table, &parent);
                /* Abort if a node has no parent or if a loop is detected */
                if (linkaddr_cmp(&parent, &linkaddr_null) ||
                    already_in_route(conn, path_len, &parent))
                {
                        printf("PATH ERROR: cannot build path for destination node: %02u:%02u. Loop detected.\n",
                               (*dest).u8[0], (*dest).u8[1]);
                        return 0;
                }
                path_len++;
        } while (!linkaddr_cmp(&parent, &sink_addr) && path_len < MAX_PATH_LENGTH);

        if (path_len > MAX_PATH_LENGTH)
        {
                /* Path is too long */
                printf("PATH ERROR: Path too long for destination node: %02u:%02u\n",
                       (*dest).u8[0], (*dest).u8[1]);
                return 0;
        }
        return path_len;
}

/**
 * @brief Prints the route (tree_path) that was just found for a destination.
 * @param conn      The collect connection structure.
 * @param route_len The length of the route.
 * @param dest      The destination address.
 */
void print_route(my_collect_conn *conn, uint8_t route_len, const linkaddr_t *dest)
{
        uint8_t i;
        printf("Sink route to node %02u:%02u:\n", (*dest).u8[0], (*dest).u8[1]);
        for (i = 0; i < route_len; i++)
        {
                printf("\t%d: %02u:%02u\n",
                       i,
                       conn->routing_table.tree_path[i].u8[0],
                       conn->routing_table.tree_path[i].u8[1]);
        }
}
