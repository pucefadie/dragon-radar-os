#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct Queue { unsigned cap, size; std::deque<std::vector<unsigned char>> items; }; typedef Queue *QueueHandle_t;
inline QueueHandle_t xQueueCreate(unsigned c,unsigned s){return new Queue{c,s,{}};}
inline int xQueueSend(QueueHandle_t q,const void *p,int){if(q->items.size()>=q->cap)return 0;auto b=static_cast<const unsigned char*>(p);q->items.emplace_back(b,b+q->size);return 1;}
inline int xQueueReceive(QueueHandle_t q,void *p,int){if(q->items.empty())return 0;memcpy(p,q->items.front().data(),q->size);q->items.pop_front();return 1;}
