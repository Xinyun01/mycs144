#include "reassembler.hh"
#include <iostream>
#include <utility>
#include <vector>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  //[[unlikely]] 是 C++20 引入的一个标准属性，用于给编译器提示该分支很可能不会被执行。它的作用是告诉编译器，
  //在代码执行过程中，这个条件分支（if 语句）发生的概率较低。这可以帮助编译器优化生成的代码，特别是在预测分支跳转和提高指令执行效率方面。
  if ( is_last_substring ) [[unlikely]] {
    total_pushed_len_ = first_index + data.length();//总计推送的长度
  }
  insert_or_store( first_index, std::move( data ) );//决定是否插入还是暂存
  write_stored_str();//写入存储函数
  if ( total_pushed_len_.has_value() && output_.writer().bytes_pushed() == *total_pushed_len_ ) [[unlikely]] {
    output_.writer().close();
  }
}

void Reassembler::insert_or_store( uint64_t first_index, std::string data )
{
  if ( first_index < next_index_ ) {
    first_index = truncate_head( first_index, data );//当前收到的自序小于期望自序截断处理
  }
  if ( first_index > next_index_ ) {
    store( first_index, std::move( data ) ); //当前收到的自序大于期望自序存储处理
  } else {
    write( std::move( data ) );//当前收到的自序恰好为期望自序直接写入
  }
}

void Reassembler::write_stored_str()
{
  for ( auto& [first_index, data] : pending_substr_ ) {
    if ( first_index <= next_index_ ) {
      auto buf = std::exchange( data, "" );
      bytes_pending_ -= buf.length();
      insert_or_store( first_index, std::move( buf ) );
    }
  }
  std::erase_if( pending_substr_, []( const auto& elem ) { return elem.second.empty(); } );
}

void Reassembler::write( std::string data )
{
  //output_ 是 ByteStream类  ByteStream类中有Writer& writer();函数
  //class Writer : public ByteStream  含有下面函数
  // void push( std::string data ); // Push data to stream, but only as much as available capacity allows.
  //output_.writer() 调用 ByteStream 的 writer() 方法，返回一个 Writer 对象的引用。
  output_.writer().push( std::move( data ) );

  //bytes_pushed：Total number of bytes cumulatively pushed to the stream 
  //多少字节已经推送到流中
  next_index_ = output_.writer().bytes_pushed();
  
}

void Reassembler::store( uint64_t first_index, std::string data )
{
  //这一段代码使用了C++17引入的条件初始化语句（if with initializer）
  //它允许你在 if 语句中先定义并初始化一个局部变量，然后使用这个变量进行条件判断。这种语法的好处是能够在条件判断时避免提前定义变量，
  //使代码更加简洁和局部化。
  //auto len = output_.writer().available_capacity() - ( first_index - next_index_ );
  //这里首先定义并初始化了一个局部变量 len，它计算当前数据可以存入的剩余容量 C++17引入（if with initializer）
  //( first_index - next_index_ )：first_index - next_index_ 是当前数据与下一个数据插入位置的距离
  //output_.writer().available_capacity()：可用的剩余容量
  //data.length() >= len ：data数据长度大于容量

  //data.erase( data.begin() + len, data.end() ) ：data.begin()：代表第一个元素
  //删除的是从 len 位置开始，直到 data 结尾的所有字符或元素。
  //  
  if ( auto len = output_.writer().available_capacity() - ( first_index - next_index_ );
       data.length() >= len ) //检查容量是否足够
  {
    data.erase( data.begin() + len, data.end() );
  }
  //空数据直接返回
  if ( data.empty() ) [[unlikely]] 
  {
    return;
  }

  //std::map<uint64_t, std::string> pending_substr_ {};
  //该 map 用来存储待重组的子串（字节片段）。uint64_t 类型的键表示子串在整个字节流中的起始位置，std::string 是子串数据本身。
  // 它负责存放那些由于前面字节的缺失而暂时无法被写入输出流的子串。等到缺失部分补齐后，再根据索引顺序将这些子串写入。

  //bytes_pending_：踪暂时存储的数据大小
  // pending_substr_.emplace  emplace() 是 std::map 或类似容器的插入方法，能高效地插入元素。
  //它在 pending_substr_ 中插入一个键值对，first_index 是键，表示数据段的起始位置，data 是值，表示要插入的数据
  if ( pending_substr_.empty() ) [[unlikely]] 
  {
    bytes_pending_ += data.length();
    //使用map的容器存储
    pending_substr_.emplace( first_index, std::move( data ) );
  }
  else 
  {
    //使用map的容器存储

    //final_index：计算first_index 后面有多少数据
    auto final_index = first_index + data.length() - 1;
    //使用 std::map::contains() 检查 pending_substr_ 是否已经包含了以 first_index 为键的元素。
    //contains() 是 C++20 引入的函数，用来判断键是否存在于 map 中
    
    if ( pending_substr_.contains( first_index ) ) 
    {
      //如果存在and 当暂存区的数据长度已经超过新接收数据的长度即新来的数据已经全部存储
      //直接return 丢弃
      if ( pending_substr_[first_index].length() >= data.length() ) 
      {
        return;
      }
      //std::exchange: map中first_index与空字符交换
      auto mapped_data = std::exchange( pending_substr_[first_index], "" );
      //现在改为空字符，暂存字符串也需要更改
      bytes_pending_ -= mapped_data.length();
      //pending_substr_.erase(first_index); 用于删除与 first_index 关联的字符串数据
      pending_substr_.erase( first_index );
    }
    
    //不存在 没看懂
    //std::erase_if 是 C++20 引入的算法，用于从容器中删除满足给定条件的所有元素。它接收两个参数：容器和一个返回布尔值的 lambda 表达式或谓词
    std::erase_if( pending_substr_, [&]( const auto& node ) 
    {
      //如果存在新传输子字符串能够完全覆盖原来存储在map容器中，bytes_pending_将删除该字符数量
      if ( node.first >= first_index && node.first + node.second.length() - 1 <= final_index ) 
      {
          bytes_pending_ -= node.second.length();
          return true;
      }
        return false;
      } 
    );

    //遍历所有map中间的元素，当插入子字符串重复时，直接return丢掉
    for ( const auto& [idx, str] : pending_substr_ ) 
    {
      if ( first_index >= idx && final_index <= idx + str.length() - 1 ) 
      {
        return;
      }
    }
    //情况1 当新来的数据正好插入为当前标签，且已经数据长度小于新的数据长度，直接删除旧数据长度 按照这个来
    //情况2 当新来的数据正好插入数据的自序大于mao中存储，且字符串的结尾数量< final_index
    //      node.first >= first_index && node.first + node.second.length() - 1 <= final_index 
    bytes_pending_ += data.length();
    pending_substr_.emplace( first_index, std::move( data ) );

    //lower_bound( first_index ) 返回指向 pending_substr_ 中 第一个键 大于等于   first_index 的元素的迭代器。
    auto begin_node = pending_substr_.lower_bound( first_index );
    //upper_bound( final_index ):它返回一个迭代器，指向 pending_substr_ 中第一个键 大于  final_index 的元素。
    auto end_node = pending_substr_.upper_bound( final_index );
    

    //begin_node：指向 pending_substr_ 中 第一个键 大于等于   first_index 的元素的迭代器
    //在代码中，std::begin(pending_substr_) 的作用是指向 pending_substr_ 容器中的第一个元素（或返回一个指向容器结尾的特殊迭代器，如果容器为空）。
    //如果begin_node：指向 pending_substr_ 中 第一个键 大于pending_substr_
    //则执行begin_node = std::prev( begin_node );   std::prev( begin_node )：调用 std::prev 函数会返回 begin_node 前一个元素的迭代器
    //让它指向插入位置之前的可能需要合并的那个元素，以便后续合并区间操作能够正确进行
    if ( begin_node != std::begin( pending_substr_ ) ) 
    {
      begin_node = std::prev( begin_node );
    }
    //begin_node调整开头保证所有元素没有泄露
    //改for循环的目的是合并map中的重叠元素并进行释放
    for ( auto node = begin_node; std::next( node ) != end_node; ++node ) 
    {
      auto next_node = std::next( node );
      auto this_final_index = node->first + node->second.length() - 1;
      auto next_first_index = next_node->first;
      //如果前一个元素的结尾大于后一个元素的开始说明这2个元素重合可以进行合并处理，bytes_pending_也需要减去重复的部分
      if ( this_final_index >= next_first_index ) [[likely]] 
      {
        auto len = this_final_index - next_first_index + 1;
        bytes_pending_ -= len;
        //删除当前元素重复的部分
        node->second.erase( node->second.begin() + node->second.length() - len, node->second.end() );
      }
    }
  }
}

uint64_t Reassembler::truncate_head( uint64_t old_index, std::string& data )//first_index = truncate_head( first_index, data );//当前收到的自序小于期望自序截断处理
{
  data.erase( 0, next_index_ - old_index );//使用引用对data进行截断处理
  return next_index_;
}

uint64_t Reassembler::bytes_pending() const
{
  return bytes_pending_;
}


