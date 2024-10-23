#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <list>
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
    , current_time_( 0 )
    , ack_( 0 )
    , in_flight_cnt_( 0 )
    , expire_time_( UINT64_MAX )
    , retrans_cnt_( 0 )
    , window_size_( 1 )
    , rto_( initial_RTO_ms )
    , current_seq_( isn.unwrap( isn, 0 ) )
    , zero_point_( isn )
    , outstanding_msg_()
    , is_fin_sent( false )
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

  uint64_t unwarp( const Wrap32& seq );
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

private:
  // Variables initialized in constructor
  ByteStream input_;//存储待发送的数据流
  Wrap32 isn_;      //表示初始序列号（ISN），用于标识 TCP 连接的开始序列号
  uint64_t initial_RTO_ms_;//初始重传超时（RTO）的值，单位为毫秒。用作连接开始时的超时设置
  uint64_t current_time_;//当前时间
  uint64_t ack_;//当前的确认号，表示已经被确认接收的字节序列号
  uint64_t in_flight_cnt_;//当前已发送但尚未被确认的字节数
  uint64_t expire_time_;//超时时间，表示下一个超时事件的时间点。如果超时发生，则会重传消息
  uint64_t retrans_cnt_;//记录连续重传的次数。用于跟踪重传的次数，以便在发生多个重传时采取不同的策略。
  uint64_t window_size_;//表示当前接收窗口的大小，控制可以接收的数据量。窗口大小为 0 时，发送方不能发送新数据
  uint64_t rto_;//重传超时时间，表示下一个重传的时间间隔。可以根据网络状况动态调整
  uint64_t current_seq_;//当前的序列号，表示下一个要发送的数据的序列号。随着数据的发送而递增
  Wrap32 zero_point_;//用于序列号的参考点，帮助处理序列号的包装
  std::deque<TCPSenderMessage> outstanding_msg_;//存储所有已发送但尚未确认的消息。用于跟踪消息状态和处理重传
  bool is_fin_sent;//标志位，指示 FIN（结束）标志是否已发送。如果发送了 FIN，则不再发送新的数据
};
