#pragma once

#include "reassembler.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"

class TCPReceiver
{
public:
  // Construct with given Reassembler
  explicit TCPReceiver( Reassembler&& reassembler )
    : reassembler_( std::move( reassembler ) )
    , zero_point_( 0 )
    , is_syn( false )
    , is_fin( false )
    , is_last_string( false )
  {}

  /*
   * The TCPReceiver receives TCPSenderMessages, inserting their payload into the Reassembler
   * at the correct stream index.
   */
  void receive( TCPSenderMessage message );

  // The TCPReceiver sends TCPReceiverMessages to the peer's TCPSender.
  TCPReceiverMessage send() const;

  // Access the output (only Reader is accessible non-const)
  const Reassembler& reassembler() const { return reassembler_; }
  Reader& reader() { return reassembler_.reader(); }
  const Reader& reader() const { return reassembler_.reader(); }
  const Writer& writer() const { return reassembler_.writer(); }

private:
  Reassembler reassembler_;
  Wrap32 zero_point_;  // zero_point，SYN帧传递
  bool is_syn;         // 表示 SYN 帧是否出现过
  bool is_fin;         // 表示 FIN 帧是否出现过
  bool is_last_string; // 表示没有多余数据，可以关闭 reassembler
};
