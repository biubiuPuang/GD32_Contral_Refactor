/* 主机验证：实际使用的去重/漏包统计模块，不访问开发板。 */
#include "packet_loss_stats.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    PacketLossStats stats;
    uint32_t i;
    PacketLossStats_Reset(&stats, 10U);
    /* 丢失首包1、尾包10及中间包5，重复3、乱序4，不得掩盖漏包。 */
    assert(PacketLossStats_Accept(&stats, 2U, 100U) == 1);
    assert(PacketLossStats_Accept(&stats, 3U, 600U) == 1);
    assert(PacketLossStats_Accept(&stats, 3U, 610U) == 0);
    assert(PacketLossStats_Accept(&stats, 6U, 1600U) == 1);
    assert(PacketLossStats_Accept(&stats, 4U, 2100U) == 1);
    assert(PacketLossStats_Accept(&stats, 7U, 2600U) == 1);
    assert(PacketLossStats_Accept(&stats, 8U, 3100U) == 1);
    assert(PacketLossStats_Accept(&stats, 9U, 3600U) == 1);
    assert(stats.unique == 7U && stats.target - stats.unique == 3U);
    assert(stats.duplicates == 1U && stats.out_of_order == 1U);
    assert(PacketLossStats_BitCount(&stats) == stats.unique);
    stats.seen[0] ^= 1U; /* 模拟实际发现的位图破坏，审计必须识别计数不一致。 */
    assert(PacketLossStats_BitCount(&stats) != stats.unique);
    stats.seen[0] ^= 1U;
    assert(stats.gaps_over_85ms == 1U && stats.max_gap_ticks == 1000U);
    assert(PacketLossStats_Accept(&stats, 0U, 0U) == -1);
    assert(PacketLossStats_Accept(&stats, 11U, 0U) == -1);
    PacketLossStats_Reset(&stats, 2U);
    assert(PacketLossStats_Accept(&stats, 1U, 0xFFFFFF00U) == 1);
    assert(PacketLossStats_Accept(&stats, 2U, 0x00000100U) == 1);
    assert(stats.max_gap_ticks == 512U && stats.gaps_over_85ms == 0U);
    PacketLossStats_Reset(&stats, PLT_MAX_PACKETS);
    for(i = 1U; i <= PLT_MAX_PACKETS; ++i)
        assert(PacketLossStats_Accept(&stats, (uint16_t)i, i * 500U) == 1);
    for(i = PLT_MAX_PACKETS; i > 0U; --i)
        assert(PacketLossStats_Accept(&stats, (uint16_t)i, 0U) == 0);
    assert(stats.unique == PLT_MAX_PACKETS && stats.duplicates == PLT_MAX_PACKETS);
    assert(PacketLossStats_BitCount(&stats) == PLT_MAX_PACKETS);
    assert(stats.max_gap_ticks == 500U);
    PacketLossStats_Reset(&stats, PLT_MAX_PACKETS + 1U);
    assert(stats.target == 0U && PacketLossStats_Accept(&stats, 1U, 0U) == -1);
    puts("PASS: leading/trailing/interior loss, deduplication, out-of-order, invalid sequence, timer wrap, 10000-packet boundary.");
    return 0;
}
