#include <stdbool.h>
#include <stdio.h>
#include "my_collect.h"
#include <string.h>
#include <stdint.h>

#ifndef SRDCP_GRAPH_MIN_PRR
#define SRDCP_GRAPH_MIN_PRR 40
#endif
#ifndef SRDCP_GRAPH_PRR_WEIGHT
#define SRDCP_GRAPH_PRR_WEIGHT 4
#endif
#ifndef SRDCP_GRAPH_LOAD_WEIGHT
#define SRDCP_GRAPH_LOAD_WEIGHT 1
#endif

static int find_route_tree(my_collect_conn *conn, const linkaddr_t *dest);
static int find_route_graph(my_collect_conn *conn, const linkaddr_t *dest);

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

static int node_index_of(const linkaddr_t nodes[], int count, const linkaddr_t *addr)
{
        int i;
        for (i = 0; i < count; i++)
        {
                if (linkaddr_cmp(&nodes[i], addr))
                        return i;
        }
        return -1;
}

static srdcp_graph_node *graph_get_node(srdcp_graph_state *graph, const linkaddr_t *addr)
{
        int i;
        for (i = 0; i < MAX_NODES; i++)
        {
                if (graph->nodes[i].used && linkaddr_cmp(&graph->nodes[i].node, addr))
                        return &graph->nodes[i];
        }
        return NULL;
}

static bool edge_is_fresh(const srdcp_graph_edge *edge)
{
        if (edge->last_update == 0)
                return false;
        clock_time_t now = clock_time();
        return (clock_time_t)(now - edge->last_update) <= SRDCP_INFO_MAX_AGE;
}

static uint16_t edge_cost(const srdcp_graph_edge *edge)
{
        uint16_t hop_penalty = SRDCP_GRAPH_HOP_WEIGHT;
        uint16_t prr_penalty = (uint16_t)((100U - edge->prr) * SRDCP_GRAPH_PRR_WEIGHT);
        uint16_t load_penalty = (uint16_t)(edge->load * SRDCP_GRAPH_LOAD_WEIGHT);
        return (uint16_t)(hop_penalty + prr_penalty + load_penalty);
}

static int ensure_node(linkaddr_t nodes[], srdcp_graph_node *ptrs[], int *count, int max,
                       srdcp_graph_state *graph, const linkaddr_t *addr)
{
        int idx = node_index_of(nodes, *count, addr);
        if (idx >= 0)
                return idx;
        if (*count >= max)
                return -1;
        linkaddr_copy(&nodes[*count], addr);
        ptrs[*count] = graph_get_node(graph, addr);
        idx = *count;
        (*count)++;
        return idx;
}

/**
 * @brief Finds a route from the SINK to a destination by backtracking through parents.
 * @param conn The collect connection structure (at the SINK).
 * @param dest The destination address.
 * @return The path length, or 0 on error (no route, loop detected, or path too long).
 * @details The path is constructed by traversing the parent dictionary backwards from the destination.
 */
static int find_route_tree(my_collect_conn *conn, const linkaddr_t *dest)
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

static int find_route_graph(my_collect_conn *conn, const linkaddr_t *dest)
{
        if (!conn->is_sink)
                return 0;
        if (linkaddr_cmp(dest, &sink_addr))
                return 0;

        linkaddr_t nodes[MAX_NODES];
        srdcp_graph_node *ptrs[MAX_NODES];
        int count = 0;

        memset(nodes, 0, sizeof(nodes));
        memset(ptrs, 0, sizeof(ptrs));

        if (ensure_node(nodes, ptrs, &count, MAX_NODES, &conn->graph, &sink_addr) < 0)
                return 0;

        /* Ensure destination exists in the index even if we have little data */
        if (ensure_node(nodes, ptrs, &count, MAX_NODES, &conn->graph, dest) < 0)
                return 0;

        int i;
        for (i = 0; i < MAX_NODES && count < MAX_NODES; i++)
        {
                if (!conn->graph.nodes[i].used)
                        continue;
                ensure_node(nodes, ptrs, &count, MAX_NODES, &conn->graph, &conn->graph.nodes[i].node);
        }

        /* Include neighbor endpoints */
        int gi;
        for (gi = 0; gi < MAX_NODES && count < MAX_NODES; gi++)
        {
                if (!conn->graph.nodes[gi].used)
                        continue;
                srdcp_graph_node *gn = &conn->graph.nodes[gi];
                uint8_t e;
                for (e = 0; e < gn->neighbor_count; e++)
                {
                        ensure_node(nodes, ptrs, &count, MAX_NODES, &conn->graph, &gn->neighbors[e].neighbor);
                }
        }

        int dest_idx = node_index_of(nodes, count, dest);
        if (dest_idx < 0)
                return 0;

        if (count <= 1)
                return 0;

        uint16_t dist[MAX_NODES];
        int16_t prev[MAX_NODES];
        uint8_t visited[MAX_NODES];

        for (i = 0; i < count; i++)
        {
                dist[i] = 0xFFFF;
                prev[i] = -1;
                visited[i] = 0;
        }
        dist[0] = 0; /* sink */

        int iter;
        for (iter = 0; iter < count; iter++)
        {
                int u = -1;
                uint16_t best = 0xFFFF;
                for (i = 0; i < count; i++)
                {
                        if (!visited[i] && dist[i] < best)
                        {
                                best = dist[i];
                                u = i;
                        }
                }
                if (u < 0 || best == 0xFFFF)
                        break;

                visited[u] = 1;
                if (u == dest_idx)
                        break;

                /* Direct neighbors from this node */
                srdcp_graph_node *node = ptrs[u];
                if (node)
                {
                        uint8_t eidx;
                        for (eidx = 0; eidx < node->neighbor_count; eidx++)
                        {
                                srdcp_graph_edge *edge = &node->neighbors[eidx];
                                if (!edge_is_fresh(edge) || edge->prr < SRDCP_GRAPH_MIN_PRR)
                                        continue;
                                int v = node_index_of(nodes, count, &edge->neighbor);
                                if (v < 0)
                                        continue;
                                uint16_t cost = edge_cost(edge);
                                uint32_t alt = (uint32_t)dist[u] + cost;
                                if (alt < dist[v])
                                {
                                        dist[v] = (uint16_t)alt;
                                        prev[v] = u;
                                }
                        }
                }

                /* Implicit reverse edges: other owners that list this node */
                for (gi = 0; gi < MAX_NODES; gi++)
                {
                        if (!conn->graph.nodes[gi].used)
                                continue;
                        srdcp_graph_node *gn = &conn->graph.nodes[gi];
                        uint8_t eidx;
                        for (eidx = 0; eidx < gn->neighbor_count; eidx++)
                        {
                                srdcp_graph_edge *edge = &gn->neighbors[eidx];
                                if (!linkaddr_cmp(&edge->neighbor, &nodes[u]))
                                        continue;
                                if (!edge_is_fresh(edge) || edge->prr < SRDCP_GRAPH_MIN_PRR)
                                        continue;
                                int v = node_index_of(nodes, count, &gn->node);
                                if (v < 0)
                                        continue;
                                uint16_t cost = edge_cost(edge);
                                uint32_t alt = (uint32_t)dist[u] + cost;
                                if (alt < dist[v])
                                {
                                        dist[v] = (uint16_t)alt;
                                        prev[v] = u;
                                }
                        }
                }
        }

        if (dist[dest_idx] == 0xFFFF)
                return 0;

        init_routing_path(conn);
        int idx = dest_idx;
        uint8_t path_len = 0;
        while (idx > 0 && idx >= 0)
        {
            if (path_len >= MAX_PATH_LENGTH)
                    return 0;
            linkaddr_copy(&conn->routing_table.tree_path[path_len], &nodes[idx]);
            idx = prev[idx];
            if (idx < 0)
                    break;
            path_len++;
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

int find_route(my_collect_conn *conn, const linkaddr_t *dest)
{
        int len = find_route_graph(conn, dest);
        if (len > 0)
        {
                printf("Graph route selected len=%d\n", len);
                return len;
        }
        len = find_route_tree(conn, dest);
        if (len > 0)
        {
                printf("Fallback tree route len=%d\n", len);
        }
        return len;
}
