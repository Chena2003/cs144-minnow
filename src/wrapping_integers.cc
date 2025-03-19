#include "wrapping_integers.hh"
#include <cmath>
#include <cstdint>

using namespace std;

/*
** wrap 就是将 uint64_t -> uint32_t
*/
Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + n;
}

/*
** unwrap 就是将 uint32_t -> uint64_t，要求转换得到结果距离 checkpoint 最近
** 因此，我们在 checkpoint 左右移动 raw_value 的值（加上或减去 MOD ），得到两个 offset
** 比较这两个 offset 值，选取较小者。再用 checkpoint 加上 offset (偏移量)
*/
uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  constexpr uint64_t MOD = 1ULL << 32;

  // 方案一：raw_value_ - zero_point
  uint32_t raw_value = raw_value_ - zero_point.raw_value_;
  uint32_t checkpoint_value = static_cast<uint32_t>( checkpoint );

  // 方案二：checkpoint + zero_point
  // uint32_t raw_value = raw_value_;
  // uint32_t checkpoint_value = static_cast<uint32_t>( checkpoint + zero_point.raw_value_ );

  uint32_t offset1; // 表示得到结果在checkpoint的右边
  uint32_t offset2; // 表示得到结果在checkpoint的左边

  offset1 = ( raw_value - checkpoint_value ) % MOD; // 等价于 (MOD - checkpoint_value + raw_value) % MOD
  offset2 = ( checkpoint_value - raw_value ) % MOD; // 等价于 (MOD - raw_value + checkpoint_value) % MOD

  // 比较两个 offset 值
  if ( offset1 < offset2 )
    return checkpoint + offset1; // offset1 要加
  else                           // offset2 要减
    return ( checkpoint >= offset2 ) ? checkpoint - offset2
                                     : ( MOD - offset2 + checkpoint ); // 特判 checkpoint 小于 offset 的情况
}
