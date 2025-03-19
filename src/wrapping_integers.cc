#include "wrapping_integers.hh"
#include <cmath>
#include <cstdint>

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + n;
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  constexpr uint64_t MOD = 1ULL << 32;
  uint32_t raw_value = raw_value_;
  uint32_t checkpoint_value = static_cast<uint32_t>( checkpoint + zero_point.raw_value_ );

  uint32_t offset1;
  uint32_t offset2;

  offset1 = ( raw_value - checkpoint_value ) % MOD; // 等价于 (MOD - checkpoint_value + raw_value) % MOD
  offset2 = ( checkpoint_value - raw_value ) % MOD; // 等价于 (MOD - raw_value + checkpoint_value) % MOD

  if ( offset1 < offset2 )
    return checkpoint + offset1;
  else
    return ( checkpoint >= offset2 ) ? checkpoint - offset2 : ( MOD - offset2 + checkpoint );
}
