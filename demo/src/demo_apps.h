#ifndef DEMO_APPS_H
#define DEMO_APPS_H

void demo_oob_entry(void *p1, void *p2, void *p3);

void demo_victim_entry(void *p1, void *p2, void *p3);

void demo_crossapp_entry(void *p1, void *p2, void *p3);

void demo_demand_entry(void *p1, void *p2, void *p3);

void demo_subpage_oob_entry(void *p1, void *p2, void *p3);

void demo_stackoverflow_entry(void *p1, void *p2, void *p3);

void demo_globals_entry(void *p1, void *p2, void *p3);

void demo_tlb_hit_entry(void *p1, void *p2, void *p3);

void demo_tlb_uaf_entry(void *p1, void *p2, void *p3);

void demo_tlb_range_entry(void *p1, void *p2, void *p3);

void demo_ipc_valid_entry(void *p1, void *p2, void *p3);

void demo_ipc_invalid_entry(void *p1, void *p2, void *p3);

void demo_ipc_crossapp_entry(void *p1, void *p2, void *p3);

void demo_semaphore_entry(void *p1, void *p2, void *p3);

void demo_tlb_verify_entry(void *p1, void *p2, void *p3);

#endif
