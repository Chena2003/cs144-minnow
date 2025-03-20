#include "tcp_sender.hh"
#include "tcp_config.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"
#include <cstdint>
#include <memory>
#include <numeric>

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return std::accumulate( outstanding_segments_seq.begin(),
                          outstanding_segments_seq.end(),
                          0ull,
                          []( uint64_t sum, auto item ) { return sum + item.second->sequence_length(); } );
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmission_cnts_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  if ( input_.has_error() ) {
    transmit( TCPSenderMessage( isn_, false, "", false, true ) );
    return;
  }

  // TCPSenderMessage sm(isn_, false, "", false, false);
  // 这里 max_size 还要减去syn和fin的大小
  uint64_t max_size = std::min( TCPConfig::MAX_PAYLOAD_SIZE, windows_size_ );

  // windows_size_ 为 0， 如何处理？
  if ( !windows_size_ )
    max_size = 1;

  // SYN帧
  bool is_syn_( is_syn );
  if ( is_syn ) {
    is_syn = false;
    rto_ms_ = base_rto_ms_;
    --max_size;
  }

  // 取出数据
  std::string data = get_data_( max_size );

  // FIN 帧，需要返回空帧
  if ( !is_syn_ && data.empty() )
    return;

  windows_size_ -= data.size(); // windows size 减去 data 大小

  std::shared_ptr<TCPSenderMessage> sm
    = std::make_shared<TCPSenderMessage>( isn_, is_syn_, std::move( data ), false, false );

  // 插入未应答段
  outstanding_segments_seq.insert( { isn_, sm } );
  outstanding_segments_time.insert( { no_++, sm } );

  isn_ = isn_ + sm->sequence_length();
  // transmit(std::move(*sm)); // std::move 之后 *sm 是否存在
  transmit( *sm ); // std::move 之后 *sm 是否存在
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage { isn_, false, "", false, false };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // 错误直接返回
  if ( msg.RST )
    input_.set_error();

  // 更新 windows_size
  windows_size_ = msg.window_size;

  // 没有 ackno
  if ( !msg.ackno.has_value() )
    return;
  Wrap32 ackno = msg.ackno.value();

  // 删除确认的数据
  bool is_new_ack = false;
  for ( auto it = outstanding_segments_seq.begin(); it != outstanding_segments_seq.end(); ) {
    auto data = it->second;
    auto data_start = it->first;
    auto data_end = data_start + data->sequence_length();

    if ( data_end <= ackno ) {
      // 在outstanding_segments_time删除已经确认的数据
      // 可以考虑 结构体
      // Wrap32 序号
      // uint64_t 时间序号
      // std::shared_ptr<TCPSenderMessage>
      // 但是如何找到最早的时间序号的segment
      // outstanding_segments_time.erase(it->first);
      it->second.reset();
      it = outstanding_segments_seq.erase( it );
      is_new_ack = true;
    } else
      break;
  }

  if ( is_new_ack ) {
    base_rto_ms_ = initial_RTO_ms_;
    rto_ms_ = base_rto_ms_;
    consecutive_retransmission_cnts_ = 0;
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( outstanding_segments_seq.empty() )
    return;

  // 更新 rto_ms_
  rto_ms_ -= std::min( rto_ms_, ms_since_last_tick );

  if ( !rto_ms_ ) {
    auto it = outstanding_segments_time.begin();
    while( it->second.unique() ) {
      
      it = outstanding_segments_time.erase(it);

    }

    transmit( *( it->second ) );
    // outstanding_segments_time.erase( outstanding_segments_time.begin() );

    if ( windows_size_ ) {
      base_rto_ms_ *= 2;
      rto_ms_ = base_rto_ms_;
      ++consecutive_retransmission_cnts_;

      // 关闭 TCP 链接
      if ( consecutive_retransmission_cnts_ >= TCPConfig::MAX_RETX_ATTEMPTS ) {
        input_.set_error();
      }
    }
  }
}

std::string TCPSender::get_data_( uint64_t num )
{
  if ( !num || ( !input_.reader().bytes_buffered() && data_.empty() ) )
    return std::string {};

  while ( data_.size() < num ) {
    std::string_view t = input_.reader().peek();

    if ( t.empty() )
      break;

    input_.reader().pop( t.size() );
    data_ += t;
  }

  uint64_t offset = std::min( data_.size(), num );
  std::string str = data_.substr( 0, offset );
  data_ = data_.substr( offset );

  return str;
}