#pragma once

#include "byte_stream.hh"
#include <map>
#include <optional>
class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.
  explicit Reassembler( ByteStream&& output ) : output_( std::move( output ) ) {}

  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring );

  // How many bytes are stored in the Reassembler itself?
  uint64_t bytes_pending() const;  //多少字节已经存储在Reassembler

  // Access output stream reader
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // Access output stream writer, but const-only (can't write from outside)
  const Writer& writer() const { return output_.writer(); }

private:
  void insert_or_store( uint64_t first_index, std::string data );//插入还是储藏

  void write_stored_str();

  void write( std::string data );//写入

  void store( uint64_t first_index, std::string data );//存储

  uint64_t truncate_head( uint64_t old_index, std::string& data ); //截断头部  该函数会截断掉这些已经处理的字节，只保留尚未处理的部分。

private:
  ByteStream output_; // the Reassembler writes to this ByteStream    这是 Reassembler 类中的一个 ByteStream 对象，重组完成的字节数据会通过它写入输出流。

  std::map<uint64_t, std::string> pending_substr_ {};//该 map 用来存储待重组的子串（字节片段）。uint64_t 类型的键表示子串在整个字节流中的起始位置，std::string 是子串数据本身。
                                                     // 它负责存放那些由于前面字节的缺失而暂时无法被写入输出流的子串。等到缺失部分补齐后，再根据索引顺序将这些子串写入。

  uint64_t bytes_pending_ {}; //表示当前存储在 pending_substr_ 中、等待重组的字节数。这是为了跟踪暂时存储的数据大小，便于检查和管理数据的剩余量。

  uint64_t next_index_ {}; //这是下一个期望接收到的字节在整个数据流中的位置。Reassembler 在写入字节流时，会从 next_index_ 开始处理。

  //这是一个可选值，用来存储整个字节流的总长度。只有当最后一段数据到达时，Reassembler 才会知道整个流的长度，所以在这之前它是 std::nullopt。
  std::optional<uint64_t> total_pushed_len_ { std::nullopt }; 
};
