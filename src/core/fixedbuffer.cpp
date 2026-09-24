#include "fixedbuffer.h"


// 模板显式实例化定义：使用 FixedBuffer 的通用模板实现，为 SmallBuffer 和 LargeBuffer 各生成一份常用容量的实现。
// .h 只需要声明，模板显式实例化定义会在 .cpp 上完成，并 D_API_EXPORTED 标记导出。
template class D_API_EXPORTED FixedBuffer<kSmallBufferSize>;
template class D_API_EXPORTED FixedBuffer<kLargeBufferSize>;
