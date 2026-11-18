#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct MockQueue { size_t limit, size; std::deque<std::vector<unsigned char>> items; };
using QueueHandle_t=MockQueue*;
inline QueueHandle_t xQueueCreate(size_t limit,size_t size) { return new MockQueue{limit,size,{}}; }
inline int xQueueSend(QueueHandle_t q,const void *p,int) { if(q->items.size()==q->limit) return 0; const auto *b=static_cast<const unsigned char*>(p); q->items.emplace_back(b,b+q->size); return 1; }
inline int xQueueReceive(QueueHandle_t q,void *p,int) { if(q->items.empty()) return 0; memcpy(p,q->items.front().data(),q->size); q->items.pop_front(); return 1; }
