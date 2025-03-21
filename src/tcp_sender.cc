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
  return std::accumulate( outstanding_segments_time.begin(),
                          outstanding_segments_time.end(),
                          0ull,
                          []( uint64_t sum, auto item ) { return sum + item.second->sequence_length(); } );
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmission_cnts_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // RST 位为真
  if ( input_.has_error() ) {
    transmit( TCPSenderMessage( isn_, false, "", false, true ) );
    return;
  }

  bool is_syn_ = false;
  bool can_output = false;
  bool last_output = false;

  // is_zero_window_size 为真，需要加一
  // is_syn 为真且 no_ack (开始时没有应答帧，需要发送syn建立连接)
  window_size_ += is_zero_window_size + ( is_syn & no_ack );

  // 判断当前窗口是否能装下数据
  if ( reader().bytes_buffered() + data_.size() + is_syn + !is_fin <= window_size_ )
    can_output = true;

  // 要尽可能填充满 window_size_ 的大小， 通过多次分段发送
  while ( window_size_ ) {

    // 每次发送段最多为 TCPConfig::MAX_PAYLOAD_SIZE
    uint64_t max_size = std::min( TCPConfig::MAX_PAYLOAD_SIZE, window_size_ );

    // syn 帧
    if ( is_syn ) {
      is_syn = false;
      is_syn_ = true;
      rto_ms_ = base_rto_ms_; // 开启定时器

      --max_size; // is_syn 占一位
    }

    // 取出数据
    std::string data = get_data_( max_size );

    // 防止重复发送 fin 帧
    if ( is_fin && data.empty() )
      return;

    // 最后一个发送段
    if ( !reader().bytes_buffered() && data_.empty() )
      last_output = true;

    // 如果当前窗口可以全部输出，当前为最后一个发送段，且is_closed()为真
    if ( can_output && last_output && writer().is_closed() )
      is_fin = true;

    // 如果非 syn 和 fin 帧数据为空，直接返回
    if ( !is_syn_ && !is_fin && data.empty() )
      return;

    // 数据段
    std::shared_ptr<TCPSenderMessage> sm
      = std::make_shared<TCPSenderMessage>( isn_, is_syn_, std::move( data ), is_fin, false );

    window_size_ -= sm->sequence_length(); // 更新window_size

    // 插入未应答段
    outstanding_segments_time.insert( { no_++, sm } );

    isn_ = isn_ + sm->sequence_length();    // 更新 isn
    total_isn_no_ += sm->sequence_length(); // 累计总的发射序号，作为receive的checkpoint
    transmit( std::move( *sm ) );           // std::move 之后 *sm 是否存在
  }

  is_zero_window_size = false; // is_zero_window_size 只能用一次
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage { isn_, false, "", false, input_.has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  // rst为真，直接返回
  if ( msg.RST )
    input_.set_error();

  is_zero_window_size = !msg.window_size; // 设置 is_zero_window_size
  no_ack = false;                         // 设置 no_ack
  receive_window_size_ = msg.window_size; // 更新收到窗口大小
  // 更新当前窗口大小（需要减去还未确认数据）
  window_size_
    = receive_window_size_ - std::min( static_cast<uint64_t>( msg.window_size ), sequence_numbers_in_flight() );

  // 没有 ackno
  if ( !msg.ackno.has_value() )
    return;

  Wrap32 ackno = msg.ackno.value();

  // 当前应答帧ackno 大于发送数据的长度（不在数据范围内）
  if ( ackno.unwrap( zero_point_, total_ack_no_ ) > isn_.unwrap( zero_point_, total_isn_no_ ) )
    return;

  // 按照发送数据顺序，删除确认的数据
  bool is_new_ack = false; // 新确认帧
  for ( auto it = outstanding_segments_time.begin(); it != outstanding_segments_time.end(); ) {
    auto data = it->second;
    auto data_start = data->seqno;
    auto data_size = data->sequence_length();
    auto data_end = data_start + data->sequence_length();

    // 确认数据帧
    if ( data_end <= ackno ) {
      total_ack_no_ += data_size;                 // 更新确认数据总数
      it = outstanding_segments_time.erase( it ); // 删除该数据段
      is_new_ack = true;
    } else // 否则直接退出，避免套环
      break;
  }

  // 有新的确认帧
  if ( is_new_ack ) {
    base_rto_ms_ = initial_RTO_ms_;       // base_rto_ms 设置为初始值
    rto_ms_ = base_rto_ms_;               // rto_ms 设置为初始值
    consecutive_retransmission_cnts_ = 0; // 连续重发数据段数设置0
  }

  // 更新当前窗口
  window_size_
    = receive_window_size_ - std::min( static_cast<uint64_t>( msg.window_size ), sequence_numbers_in_flight() );
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // 没有待确认数据，直接返回
  if ( outstanding_segments_time.empty() )
    return;

  // 更新 rto_ms_
  rto_ms_ -= std::min( rto_ms_, ms_since_last_tick );

  // rto_ms 为 0，定时器到时
  if ( !rto_ms_ ) {
    auto it = outstanding_segments_time.begin(); // 当前最早发送数据段

    transmit( *( it->second ) );

    // 收到窗口大小为 0 或者
    // no_ack 为真，表明当前未确认 syn 帧
    if ( receive_window_size_ || no_ack ) {
      base_rto_ms_ *= 2;                  // base_rto_ms 翻倍
      ++consecutive_retransmission_cnts_; // 连续重发数据段数

      // 关闭 TCP 链接
      if ( consecutive_retransmission_cnts_ >= TCPConfig::MAX_RETX_ATTEMPTS ) {
        input_.set_error();
      }
    }

    rto_ms_ = base_rto_ms_; // 更新 rto_ms
  }
}

// 获取num大小的数据
std::string TCPSender::get_data_( uint64_t num )
{
  // 没有数据
  if ( !num || ( !reader().bytes_buffered() && data_.empty() ) )
    return std::string {};

  // 获取num大小数据
  while ( data_.size() < num ) {
    std::string_view t = input_.reader().peek();

    if ( t.empty() )
      break;

    input_.reader().pop( t.size() );
    data_ += t;
  }

  uint64_t offset = std::min( data_.size(), num );
  std::string str = data_.substr( 0, offset ); // 截取 num 位
  data_ = data_.substr( offset );              // 多取出的数据

  return str;
}