#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <queue>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) )
    , isn_( isn )
    , initial_RTO_ms_( initial_RTO_ms )
    , zero_point_( isn )
    , base_rto_ms_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

private:
  // Variables initialized in constructor
  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  std::map<uint64_t, std::shared_ptr<TCPSenderMessage>> outstanding_segments_time {};
  Wrap32 zero_point_;                           // 存储偏移量
  std::string data_ {};                         // 存储多取出来的数据
  uint64_t consecutive_retransmission_cnts_ {}; // 连续重发数据段数
  uint64_t window_size_ {};                     // 窗口大小
  uint64_t receive_window_size_ {};             // 收到窗口大小
  uint64_t rto_ms_ {};                          // 当前 rts 时间
  uint64_t base_rto_ms_ {};                     // rts 的倍数
  uint64_t no_ {};                              // 发送数据段顺序
  uint64_t total_ack_no_ {};                    // 确认数据总数，作为ack_no unwrap的checkpoint
  uint64_t total_isn_no_ {};                    // 发送数据总数，作为isn_no unwrap的checkpoint

  bool is_syn { true };               // syn 标志，初始为真
  bool is_fin { false };              // fin 标志，初始为假
  bool is_zero_window_size { false }; // 窗口为0标志
  bool no_ack { true };               // 还未收到应答帧

  // get_data
  std::string get_data_( uint64_t num );
};
