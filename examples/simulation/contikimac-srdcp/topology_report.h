#ifndef TOPOLOGY_REPORT_H
#define TOPOLOGY_REPORT_H

void topology_report_hold_cb(void*);
void topology_report_timer_cb(void*);

void deliver_topology_report_to_sink(my_collect_conn*);
bool check_address_in_topologyreport_block(my_collect_conn*, linkaddr_t);
void send_topology_report(my_collect_conn*, uint8_t);

#endif // TOPOLOGY_REPORT_H
