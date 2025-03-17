#include "reassembler.hh"
#include <algorithm>
#include <cstdint>
#include <numeric>

using namespace std;

void Reassembler::merge_data() {
  if(map_.empty())
    return ;

  uint64_t r = 0;
  decltype(map_.begin()) rit;
  for(auto it = map_.begin(); it != map_.end(); ) {
    auto index_ = it->first;
    auto end_ = index_ + it->second.size();    

    if(r && r >= index_) {
      if(r < end_) {
        rit->second.append(std::move(it->second.substr(r - index_)));
        r = end_;
      }

      it = map_.erase(it);
    }
    else {
      r = end_; 
      rit = it;
      ++it;
    }
  }
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{  

  auto& constwriter_ = writer();
  auto& writer_ = output_.writer();
  uint64_t index = constwriter_.bytes_pushed();
  uint64_t capacity = constwriter_.available_capacity();
  uint64_t data_end = first_index + data.size();
  uint64_t end = index + capacity;

  // 处理最后的输入
  if(is_last_substring && data_end < end) {
    is_last_ = true;
  }
  
  if(first_index >= end || data_end <= index ) {
    close_writer();
    return ;
  }

  // 处理输入数据
  auto start_ = std::max(first_index, index);
  auto end_ = std::min(data_end, end);
  auto length = end_ - start_;
  auto offset = start_ - first_index;

  data = data.substr(offset, length);
  first_index = start_;

  // 插入数据 
  auto it_ = map_.find(first_index);
  if (it_ != map_.end()) {
      if (data.size() <= it_->second.size()) {
          // close_writer();
          return;
      }
      it_->second = move(data);  // 移动语义
  } else {
      map_.emplace(first_index, move(data));  // 避免拷贝
  }

  // 合并有重叠的区间
  merge_data();

  // 处理输出
  for(auto it = map_.begin(); it != map_.end(); ) {
    auto index_ = it->first;
    auto data_ = it->second;
    capacity =  constwriter_.available_capacity();
    index = constwriter_.bytes_pushed();
    auto size_ = data_.size();

    if(index_ > index || capacity == 0)
      break;

    if(index >= index_ && index <= index_ + data_.size()) {
      string& chunk = it->second;
      const uint64_t chunk_offset = index - index_;
      const uint64_t write_size = min(size_ - chunk_offset, capacity);
      writer_.push(chunk.substr(chunk_offset, write_size));
      it = map_.erase(it);
    }
    else
      ++it;
  }

  close_writer();
}

uint64_t Reassembler::bytes_pending() const
{
  // uint64_t bytes{};
  // auto count = [&bytes](const auto& item){ bytes += item.second.size(); };

  // std::for_each(map_.begin(), map_.end(), count);

  // return bytes;
  return std::accumulate(map_.begin(), map_.end(), 0ull, [](uint64_t sum, const auto& p) { return sum + p.second.size(); });
}

void Reassembler::close_writer() {
  if(is_last_ && map_.empty())
    output_.writer().close();  
}