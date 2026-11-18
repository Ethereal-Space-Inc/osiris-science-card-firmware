#if !defined(_CAN_UTIL_H_)
#define _CAN_UTIL_H_

#define DEFAULT_NODE_ID 0x70

uint8_t load_node_id(void);
int store_node_id(uint8_t node_id);

#endif
