# 伙伴内存管理

内存管理器的实现，具体代码见`/kernel/mm/buddy.c`

## 伙伴系统
* 伙伴系统将内存分割成大小为2的幂次方的块，两个伙伴块的大小相同
* 内存分配时，首先查找最适合的空闲块，如果找到的空闲块大于等于请求的大小，则将该块分配出去。如果块太大，则将其分割成两个伙伴块，直到分割出的块适合请求的大小
* 释放内存时，尝试将相邻的伙伴块合并成更大的块，以便复用

## 数据结构
* **位图数组`buddy_bitmap`**  
```c
// 位图数组，表示每个物理页的分配状态
static unsigned long buddy_bitmap[MAX_PAGE_NUM / 64];
```
存储每个页面的分配状态，每个物理页用一位表示。在伙伴系统中，通过位图来管理内存页的分配和释放。

* **空闲表`buddy_free_node`**  
```c
// 空闲表节点，表示每个空闲块的信息
typedef struct buddy_free_node {
    int page_index_start;  // 空闲块起始页的索引
    int size;              // 块的大小（以物理页为单位）
    struct buddy_free_node *prev, *next;  // 双向链表指针
} buddy_free_node;
```
空闲表维护了每个阶数（即块大小）对应的空闲块链表。每个链表的节点是一个空闲块，它包括了该空闲块的起始页索引和块的大小。

* **分配表`buddy_occu_node`**  
```c
// 分配表节点，记录已分配块的信息
typedef struct buddy_occu_node {
    int page_index_start;  // 已分配块的起始页索引
    int size;              // 块的大小（以物理页为单位）
    struct buddy_occu_node *prev, *next;  // 双向链表指针
} buddy_occu_node;
```
与空闲表原理相同，用于记录已分配的内存块。每个节点也存储了已分配块的起始页索引和块的大小。

* **进程内存块节点`process_memory_node`**
```c
// 进程内存块节点，记录每个进程的内存块地址和大小
typedef struct process_memory_node {
    int page_index_start;  // 块的起始页索引
    int size;              // 块的大小（以物理页为单位）
    struct process_memory_node *next; // 链表指针
} process_memory_node;
```

## 分配过程
* **计算所需阶数**  
```c
// 获取小于等于请求内存大小的最大合适阶数
static int calculate_order_for_request(size_t size) {
    if (size <= 0) {
        return -1;
    }
    int order = MAX_ORDER;
    size = size - 1; // 为了避免 size 恰好是 2 的幂
    while (size > 0) {
        size >>= 1;
        order--;
    }
    return order;
}
```
根据请求的内存块大小，计算所需的阶数。

* **获取页面**
```c
// 分配内存并将进程内存块添加到进程内存链表
int get_page_buddy(size_t size) {
    int total_requested_size = size; // 记录总请求的内存大小
    int remaining_size = size; // 剩余未分配的内存

    // 用于存储所有分配的内存块的起始页
    process_memory_node *process_head = NULL;

    // 持续分配内存，直到满足总请求
    while (remaining_size > 0) {
        // 计算当前剩余内存所需的最小阶数
        int required_order = calculate_order_for_request(remaining_size);

        int current_order = required_order;
        int allocated_page_start = -1;

        // 查找合适的空闲块
        while (current_order <= MAX_ORDER) {
            if (free_table.head[current_order] != NULL) {
                // 找到可用的空闲块
                buddy_free_node *node = free_table.head[current_order];
                allocated_page_start = node->page_index_start;

                // 从空闲表中移除该块
                if (free_table.head[current_order] == node) {
                    free_table.head[current_order] = node->next;
                }
                if (free_table.tail[current_order] == node) {
                    free_table.tail[current_order] = node->prev;
                }
                if (node->prev != NULL) {
                    node->prev->next = node->next;
                }
                if (node->next != NULL) {
                    node->next->prev = node->prev;
                }

                // 将块添加到分配表中
                buddy_occu_node *occu_node = (buddy_occu_node*)buddy_malloc(sizeof(buddy_occu_node));
                occu_node->page_index_start = allocated_page_start;
                occu_node->size = node->size;  // 记录块的大小
                occu_node->prev = NULL;
                occu_node->next = occu_table.head[required_order];
                if (occu_table.head[required_order] != NULL) {
                    occu_table.head[required_order]->prev = occu_node;
                }
                occu_table.head[required_order] = occu_node;
                if (occu_table.tail[required_order] == NULL) {
                    occu_table.tail[required_order] = occu_node;
                }

                // 如果块太大，需要进行分割
                while (current_order > required_order) {
                    current_order--;
                    int buddy_page = allocated_page_start + (1 << current_order);

                    // 将分割出的伙伴块加入空闲表
                    buddy_free_node *new_free_node = (buddy_free_node*)buddy_malloc(sizeof(buddy_free_node));
                    new_free_node->page_index_start = buddy_page;
                    new_free_node->size = (1 << current_order);  // 记录块的大小
                    new_free_node->prev = NULL;
                    new_free_node->next = free_table.head[current_order];
                    if (free_table.head[current_order] != NULL) {
                        free_table.head[current_order]->prev = new_free_node;
                    }
                    free_table.head[current_order] = new_free_node;
                    if (free_table.tail[current_order] == NULL) {
                        free_table.tail[current_order] = new_free_node;
                    }
                }

                // 标记该块为已分配
                set_bit(allocated_page_start);

                // 添加到进程的内存链表
                process_memory_node *new_process_node = (process_memory_node*)buddy_malloc(sizeof(process_memory_node));
                new_process_node->page_index_start = allocated_page_start;
                new_process_node->size = node->size;
                new_process_node->next = process_head;
                process_head = new_process_node;

                // 更新剩余需求的内存
                remaining_size -= node->size * 4096; // 已分配的内存大小，以字节为单位

                break; // 成功分配一个块，跳出当前循环
            }
            current_order++;
        }

        // 如果未能找到合适的空闲块，则返回错误
        if (allocated_page_start == -1) {
            // 回收已分配的内存块
            process_memory_node *temp = process_head;
            while (temp != NULL) {
                free_buddy_page(temp->page_index_start); // 假设 free_buddy_page 正确释放内存
                temp = temp->next;
            }
            return -1;  // 无法满足内存需求
        }
    }

    // 将进程的内存链表存入进程内存表
    process_mem_table.head = process_head;

    return total_requested_size;
}
```
1. 从空闲表移除该块。
2. 将该块添加到分配表中。
3. 如果该块大于请求的大小，则将其分割成两个伙伴块，并将其中一个伙伴块加入空闲表。
4. 如果没有找到合适的块，则返回-1表示分配失败。

* **释放页面**
```c
void free_buddy_page(int page_index_start) {
    process_memory_node *node = process_mem_table.head;
    while (node != NULL) {
        // 将内存块释放
        free_buddy_page(node->page_index_start);

        // 移除该进程的内存块
        process_memory_node *next_node = node->next;
        buddy_free(node);  // 假设 buddy_free 函数已实现内存释放
        node = next_node;
    }

    // 清空进程内存链表
    process_mem_table.head = NULL;
}
```
1. 从分配表中找到要释放的页面。
2. 从分配表中移除该块。
3. 释放后，尝试将相邻的伙伴块合并成更大的块。
4. 最终，将合并后的块加入空闲表中。

## 修改方案
### 使用链表代替栈结构
* **更灵活的内存管理**  
双向链表允许从两端插入和删除元素，并且每个元素都有指向前后元素的指针，避免了栈结构中常见的元素移动。

* **高效伙伴合并操作**  
合并两个伙伴块时，双向链表能够直接修改元素的指针，而不需要重新排列整个表。

* **双向链表的实现**  
`buddy_free_node`和`buddy_occu_node`结构体中增加了`prev`和`next`指针来实现双向链表。链表节点的插入和删除操作通过修改这些指针来实现。

* **插入和删除操作**  
在`get_page_buddy`和`free_buddy_page`函数中，插入和删除节点操作通过链表指针操作来实现，从而避免了栈结构中元素的移动。

### 压缩伙伴ID
* **位图存储结构**  
使用一个整型数组`unsigned long buddy_bitmap[MAX_PAGE_NUM / 64]`来存储位图。每个`unsigned long`类型可以存储 64 位的信息，因此每个数组元素对应 64 个物理页的状态。

* **位图管理**  
使用`buddy_bitmap`代替了`buddy_id`数组，`buddy_bitmap`是一个位图，每个物理页的分配状态用一个位来表示。  
通过`set_bit`、`clear_bit`和`test_bit`来操作位图，标记和检查内存页是否已分配。

* **初始化修改**  
初始化时将物理页位图的每一位清零，并在`init_buddy`中将整个内存页标记为已分配。

* **分配和释放修改**  
在分配内存时，`set_bit`用来标记物理页已分配。释放时，`clear_bit`用来标记物理页为未分配状态。
