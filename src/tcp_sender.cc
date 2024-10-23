#include "tcp_sender.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  // Your code here.返回当前已发送但未被确认的字节数
  return in_flight_cnt_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  // Your code here. 返回连续重传的次数
  return retrans_cnt_;
}

void TCPSender::push(const TransmitFunction &transmit) {
    // 检查是否处于 RST 错误状态，如果是则发送 RST 消息并停止
    if (reader().has_error()) {
        TCPSenderMessage rst_msg;
        rst_msg.RST = true;
        rst_msg.seqno = Wrap32::wrap(current_seq_, zero_point_);
        transmit(rst_msg);
        return;  // 错误状态，停止数据处理
    }

    // 处理窗口大小为 0 的情况
    bool window_zero = window_size_ == 0; //先判断是否复制
    uint64_t available_window = (window_size_ + window_zero) < in_flight_cnt_
                                ? 0
                                : window_size_ + window_zero - in_flight_cnt_;

    do {
        // 如果 FIN 已发送，直接返回，不再处理
        if (is_fin_sent) return;

        // 计算当前 payload 大小
        uint64_t payload_size = min(reader().bytes_buffered(), TCPConfig::MAX_PAYLOAD_SIZE);
        uint64_t seq_size = min(available_window, payload_size + (current_seq_ == 0));  // SYN 消息需要额外一个字节
        payload_size = seq_size;  // 实际发送的数据大小

        // 构建新的 TCP 消息
        TCPSenderMessage msg;

        // 如果还未发送 SYN，需要设置 SYN 标志
        if (current_seq_ == 0) {
            msg.SYN = true;
            payload_size--;  // SYN 占用了一个序列号
        }

        // 从 ByteStream 中读取数据到消息 payload
        while (msg.payload.size() < payload_size) {
            string_view front_view = reader().peek();
            uint64_t bytes_to_read = min(front_view.size(), payload_size - msg.payload.size());
            msg.payload += front_view.substr(0, bytes_to_read);
            input_.reader().pop(bytes_to_read);
        }

        // 如果所有数据都已发送，且窗口还有剩余，设置 FIN 标志
        if (reader().is_finished() && seq_size < available_window) {
            msg.FIN = true;
            seq_size++;  // FIN 也占用一个序列号
            is_fin_sent = true;
        }

        // 如果没有实际要发送的数据，直接返回
        if (msg.sequence_length() == 0) return;

        // 设置消息序列号
        msg.seqno = Wrap32::wrap(current_seq_, zero_point_);
        current_seq_ += msg.sequence_length();  // 更新当前序列号
        in_flight_cnt_ += msg.sequence_length();  // 更新已发送但未确认的字节数

        // 记录发送的消息
        outstanding_msg_.push_back(msg);

        // 发送消息
        transmit(msg);

        // 如果这是第一个未确认的消息，更新超时时间
        if (expire_time_ == UINT64_MAX) {
            expire_time_ = current_time_ + rto_;
        }

        // 重新计算可用窗口
        available_window = (window_size_ + window_zero) < in_flight_cnt_
                           ? 0
                           : window_size_ + window_zero - in_flight_cnt_;

    } while (reader().bytes_buffered() != 0 && available_window != 0);  // 继续发送，直到窗口用完或没有更多数据
}



TCPSenderMessage TCPSender::make_empty_message() const
{
  // Your code here.
  return { Wrap32::wrap( current_seq_, zero_point_ ), false, string(), false, reader().has_error() };
}

void TCPSender::receive(const TCPReceiverMessage& msg) {
    if (msg.ackno.has_value()) {
        uint64_t ack_from_recv = unwarp(msg.ackno.value());

        // 确认接收的 ACK 在期望范围内
        if (ack_from_recv > ack_ && ack_from_recv <= current_seq_) {
            ack_ = ack_from_recv;
            rto_ = initial_RTO_ms_;  // 重置重传超时时间
            expire_time_ = current_time_ + rto_;  // 重置超时时间
            retrans_cnt_ = 0;  // 重置重传计数

            // 移除已确认的消息
            while (!outstanding_msg_.empty()) {
                auto& front_msg = outstanding_msg_.front();
                if (unwarp(front_msg.seqno) + front_msg.sequence_length() > ack_) {
                    break;
                }
                in_flight_cnt_ -= front_msg.sequence_length();
                outstanding_msg_.pop_front();
            }

            // 如果没有未确认的消息，设置超时时间为无穷大
            if (outstanding_msg_.empty()) {
                expire_time_ = UINT64_MAX;
            }
        }
    }

    // 更新窗口大小
    window_size_ = msg.window_size;

    // 如果收到 RST，设置错误状态
    if (msg.RST) {
        writer().set_error();
    }
}


void TCPSender::tick(uint64_t ms_since_last_tick, const TransmitFunction& transmit) {
    current_time_ += ms_since_last_tick;

    // 检查是否超时
    if (expire_time_ != UINT64_MAX && current_time_ >= expire_time_) {
        // 重传最早未确认的消息
        transmit(outstanding_msg_.front());

        // 如果窗口大小不为 0，则重传计数加 1，并增加重传超时时间
        if (window_size_ != 0) {
            retrans_cnt_++;
            rto_ *= 2;  // 加倍重传超时
        }

        // 更新超时时间
        expire_time_ = current_time_ + rto_;
    }
}

uint64_t TCPSender::unwarp( const Wrap32& seq )
{
  return seq.unwrap( zero_point_, ack_ );
}
