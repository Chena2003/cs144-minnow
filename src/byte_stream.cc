#include "byte_stream.hh"
#include <cstring>

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ), buf_( capacity, '\0' ) {}

bool Writer::is_closed() const
{
  return close_;
}

void Writer::push( string data )
{
  uint64_t dsize = std::min( available_capacity(), data.size() );
  uint64_t tailsize = capacity_ - tail_;
  if ( !has_error() && !is_closed() ) {

    // 环形缓冲区通用写入逻辑（无需判断 head_ 和 tail_ 的相对位置）
    uint64_t size1 = std::min( dsize, tailsize );
    uint64_t size2 = dsize - size1;

    std::memcpy( buf_.data() + tail_, data.data(), size1 );
    std::memcpy( buf_.data(), data.data() + size1, size2 );

    tot_push_bytes_ += dsize;
    size_ += dsize;
    tail_ += dsize;
    tail_ %= capacity_;
  }
}

void Writer::close()
{
  close_ = true;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - size_;
}

uint64_t Writer::bytes_pushed() const
{
  return tot_push_bytes_;
}

bool Reader::is_finished() const
{
  return close_ && !size_;
}

uint64_t Reader::bytes_popped() const
{
  return tot_pop_bytes_;
}

string_view Reader::peek() const
{
  if ( !size_ )
    return string_view {};

  uint64_t contiguous = ( head_ < tail_ ) ? ( tail_ - head_ ) : ( capacity_ - head_ );
  return string_view( buf_.data() + head_, contiguous );
  // 可以返回多个字节的数据😅😅😅
  // return string_view(buf_.data(), 1);
}

void Reader::pop( uint64_t len )
{
  if ( has_error() )
    return;

  uint64_t t = std::min( len, size_ );

  head_ = ( head_ + t ) % capacity_;
  size_ -= t;
  tot_pop_bytes_ += t;
}

uint64_t Reader::bytes_buffered() const
{
  return size_;
}
