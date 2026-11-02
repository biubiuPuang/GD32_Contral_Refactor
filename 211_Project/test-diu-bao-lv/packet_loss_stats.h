#ifndef PACKET_LOSS_STATS_H
#define PACKET_LOSS_STATS_H

#include <stdint.h>
#define PLT_MAX_PACKETS 10000U

/* 项目自定义统计模块：位图去重，首包和尾包丢失也计入最终结果。 */
typedef struct {
    uint32_t target, unique, duplicates, out_of_order;
    uint32_t last_tick, max_gap_ticks, gaps_over_85ms;
    uint16_t highest;
    uint8_t seen[(PLT_MAX_PACKETS + 7U) / 8U];
} PacketLossStats;

void PacketLossStats_Reset(PacketLossStats *stats, uint32_t target);
/* 返回 1=首次有效序号，0=重复，-1=越界。tick 单位为 0.1ms。 */
int PacketLossStats_Accept(PacketLossStats *stats, uint16_t sequence, uint32_t tick);
/* 独立计数位图中的置位数，用于发现内存破坏，不能只相信累计计数器。 */
uint32_t PacketLossStats_BitCount(const PacketLossStats *stats);
#endif
