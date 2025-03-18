#include "reassembler.hh"
#include <algorithm>
#include <cstdint>
#include <numeric>

using namespace std;

void Reassembler::try_merge( Chunk&& new_chunk )
{
  auto it = buffer_.begin();
  while ( it != buffer_.end() ) {
    uint64_t start_ = it->start;
    uint64_t end_ = it->end;

    if ( new_chunk.end < start_ ) {
      break;
    } else if ( end_ >= new_chunk.start ) {
      // 合并重叠块
      if ( new_chunk.start > start_ ) {
        new_chunk.data = it->data.substr( 0, new_chunk.start - start_ ) + new_chunk.data;
        new_chunk.start = it->start;
      }

      if ( end_ > new_chunk.end ) {
        new_chunk.data += it->data.substr( new_chunk.end - start_ );
        new_chunk.end = end_;
      }
      it = buffer_.erase( it );
    } else {
      ++it;
    }
  }

  // 插入到正确位置（保持有序）
  auto pos = lower_bound(
    buffer_.begin(), buffer_.end(), new_chunk.start, []( const Chunk& c, uint64_t s ) { return c.start < s; } );
  buffer_.insert( pos, move( new_chunk ) );
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  auto& constwriter_ = writer();
  auto& writer_ = output_.writer();
  uint64_t curr_index = constwriter_.bytes_pushed();
  uint64_t capacity = constwriter_.available_capacity();
  uint64_t data_end = first_index + data.size();
  uint64_t max_end = curr_index + capacity;

  // 处理最后的输入
  if ( is_last_substring && data_end < max_end ) {
    is_last_ = true;
  }

  // 不符合范围，直接退出
  if ( first_index >= max_end || data_end <= curr_index ) {
    close_writer();
    return;
  }

  // 处理输入数据
  const uint64_t start = max( first_index, curr_index );
  const uint64_t end = min( data_end, max_end );
  if ( start >= end ) {
    close_writer();
    return;
  }

  auto length = end - start;
  auto offset = start - first_index;

  data = data.substr( offset, length );
  first_index = start;

  try_merge( Chunk( first_index, std::move( data ) ) );

  // 处理输出
  for ( auto it = buffer_.begin(); it != buffer_.end(); ) {
    auto index_ = it->start;
    auto data_ = it->data;
    capacity = constwriter_.available_capacity();
    curr_index = constwriter_.bytes_pushed();
    auto size_ = data_.size();

    if ( index_ > curr_index || capacity == 0 )
      break;

    if ( curr_index >= index_ && curr_index <= index_ + data_.size() ) {
      string& chunk = it->data;
      const uint64_t chunk_offset = curr_index - index_;
      const uint64_t write_size = min( size_ - chunk_offset, capacity );
      writer_.push( chunk.substr( chunk_offset, write_size ) );
      it = buffer_.erase( it );
    } else
      ++it;
  }

  close_writer();
}

uint64_t Reassembler::bytes_pending() const
{
  return accumulate(
    buffer_.begin(), buffer_.end(), 0ull, []( uint64_t sum, const Chunk& c ) { return sum + c.data.size(); } );
}

void Reassembler::close_writer()
{
  if ( is_last_ && buffer_.empty() )
    output_.writer().close();
}