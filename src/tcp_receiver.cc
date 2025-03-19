#include "tcp_receiver.hh"
#include "wrapping_integers.hh"
#include <cstdint>
#include <optional>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // RST 为真，设置错误
  if ( message.RST ) {
    reader().set_error();
    return;
  }

  // SYN 为真
  Wrap32 seqno = message.seqno;
  if ( message.SYN ) {
    is_syn = true;       // 设置 is_syn 为真
    zero_point_ = seqno; // 初始化 zero_point
  } else {
    seqno = seqno + -1; // 非 SYN 帧需要序列号需要减一
  }

  // 还没有 SYN 帧，直接返回
  if ( !is_syn )
    return;

  // FIN 为真，设置 is_fin 为真
  if ( message.FIN )
    is_fin = true;

  uint64_t checkpoint = writer().bytes_pushed();                  // checkpoint
  uint64_t first_index = seqno.unwrap( zero_point_, checkpoint ); // 确实插入位置

  // 插入数据
  reassembler_.insert( first_index, message.payload, is_last_string );

  // 若没有多余数据并且 is_fin 为真，则关闭reassembler
  if ( !reassembler_.bytes_pending() && is_fin ) {
    is_last_string = true;
    reassembler_.insert( writer().bytes_pushed(), "", is_last_string );
  }
}

TCPReceiverMessage TCPReceiver::send() const
{
  // window_size 表示当前可插入数据的大小，但需要小于 UINT16_MAX
  uint16_t window_size = std::min( writer().available_capacity(), static_cast<uint64_t>( UINT16_MAX ) );
  // rst 位为 has_error()
  bool rst = reader().has_error();
  /*
  ** ackno 表示当前需要的发送数据的Sequence Numbers
  ** is_syn 为假，返回std::nullopt
  ** is_syn 为真，返回 ackno
  ** 其实就是返回在reassembler_已经插入的数据大小(curr_index)，再转换成 Sequence Numbers
  ** **注意：** Sequence Numbers 包括 SYN 和 FIN，因此需要加上 SYN 和 FIN
  ** 但是这里 FIN 不是在一出现 FIN 标志时加上，而是要在没有数据时加上
  */
  std::optional<Wrap32> ackno
    = ( is_syn ? std::optional<Wrap32>( zero_point_ + ( writer().bytes_pushed() + 1 + is_last_string ) )
               : std::nullopt );

  return TCPReceiverMessage { ackno, window_size, rst };
}
