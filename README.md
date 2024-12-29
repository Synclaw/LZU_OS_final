# 伙伴内存管理

内存管理器的实现，具体代码见`/kernel/mm/buddy.c`

## 伙伴系统
* 伙伴系统将内存分割成大小为2的幂次方的块，通常会按照二叉树的方式进行组织，两个伙伴块的大小相同
* 内存分配时，首先查找最适合的空闲块，如果找到的空闲块大于等于请求的大小，则将该块分配出去。如果块太大，则将其分割成两个伙伴块，直到分割出的块适合请求的大小
* 释放内存时，尝试将相邻的伙伴块合并成更大的块，以便复用
## 数据结构
* **伙伴ID数组`buddy_id`**  
```c
static int buddy_id[32768];
```
存储每个页面的伙伴ID，每个物理页都有一个唯一的ID。在伙伴系统中，每一对伙伴页面通过这个ID来联系
* **空闲表`buddy_free_table`**  
```c
typedef struct buddy_free_table{
    buddy_free_node table[MAX_ORDER + 1][MAX_PAGE_NUM];
    int stack_top[MAX_ORDER + 1];
} buddy_free_table;
```
通过栈来管理这个表  
空闲表维护了每个阶数（即块大小）对应的空闲块链表。每个链表的节点是一个空闲块，它包括了该空闲块的起始页索引和伙伴ID，`stack_top`数组记录每个阶数的栈顶位置，用于实现栈式管理：分配时出栈，回收时入栈
* **分配表`buddy_occu_table`**  
```c
typedef struct buddy_occu_table{
    buddy_occu_node table[MAX_ORDER][MAX_PAGE_NUM];
    int stack_top[MAX_ORDER + 1];
} buddy_occu_table;
```
与空闲表原理相同，用于记录已分配的内存块。每个节点也存储了已分配块的起始页索引和伙伴ID
## 分配过程
* **计算所需阶数**  
```c
static int calculate_order(int size) {
    int order = 0;
    size = size - 1;  // 减1是为了处理正好是2的幂的情况
    while (size > 0) {
        size >>= 1;
        order++;
    }
    return order;
}
```
根据请求的内存块大小，计算所需的阶数
* **获取页面**
```c
int get_page_buddy(int size) {
    if (size <= 0 || size > (1 << MAX_ORDER)) {
        return -1;  // 请求的大小无效
    }
    
    int order = calculate_order(size);
    int current_order = order;
    
    while (current_order <= MAX_ORDER) {
        if (free_table.stack_top[current_order] >= 0) {
            // 找到可用块
            int stack_index = free_table.stack_top[current_order];
            int page_start = free_table.table[current_order][stack_index].page_index_start;
            int current_buddy_id = free_table.table[current_order][stack_index].buddy_id;
            
            // 从空闲表移除该块
            free_table.stack_top[current_order]--;
            
            // 将块添加到分配表
            occu_table.stack_top[order]++;
            occu_table.table[order][occu_table.stack_top[order]].page_index_start = page_start;
            occu_table.table[order][occu_table.stack_top[order]].buddy_id = current_buddy_id;
            
            // 如果块太大，进行分割
            while (current_order > order) {
                current_order--;
                int buddy_page = page_start + (1 << current_order);
                
                // 将分割出的伙伴块加入空闲表
                free_table.stack_top[current_order]++;
                free_table.table[current_order][free_table.stack_top[current_order]].page_index_start = buddy_page;
                free_table.table[current_order][free_table.stack_top[current_order]].buddy_id = buddy_id[buddy_page];
            }
            
            return page_start;
        }
        current_order++;
    }

    return -1;  // 无法找到合适的空闲块
}
```
1. 从空闲表移除该块（栈式操作）
2. 将该块添加到分配表中
3. 如果该块大于请求的大小，则将其分割成两个伙伴块，并将其中一个伙伴块加入空闲表
4. 如果没有找到合适的块，则返回-1表示分配失败
* **释放页面**
```c
void free_buddy_page(int page) {
    int found_order = -1;
    int found_index = -1;
    
    // 查找要释放的页面
    for (int order = 0; order <= MAX_ORDER; order++) {
        for (int i = 0; i <= occu_table.stack_top[order]; i++) {
            if (occu_table.table[order][i].page_index_start == page) {
                found_order = order;
                found_index = i;
                break;
            }
        }
        if (found_order != -1) break;
    }
    
    if (found_order == -1) return;  // 页面未分配，释放失败
    
    // 从分配表中移除该块
    int current_buddy_id = occu_table.table[found_order][found_index].buddy_id;
    for (int i = found_index; i < occu_table.stack_top[found_order]; i++) {
        occu_table.table[found_order][i].page_index_start = occu_table.table[found_order][i + 1].page_index_start;
        occu_table.table[found_order][i].buddy_id = occu_table.table[found_order][i + 1].buddy_id;
    }
    occu_table.stack_top[found_order]--;
    
    // 尝试合并伙伴块
    int current_order = found_order;
    int current_page = page;
    
    while (current_order < MAX_ORDER) {
        int buddy_page = current_page ^ (1 << current_order);  // 计算伙伴块的地址
        int found_buddy = 0;
        
        // 查找伙伴块
        for (int i = 0; i <= free_table.stack_top[current_order]; i++) {
            if (free_table.table[current_order][i].page_index_start == buddy_page) {
                // 找到伙伴块，可以合并
                found_buddy = 1;
                
                // 从空闲表中移除伙伴块
                for (int j = i; j < free_table.stack_top[current_order]; j++) {
                    free_table.table[current_order][j].page_index_start = free_table.table[current_order][j + 1].page_index_start;
                    free_table.table[current_order][j].buddy_id = free_table.table[current_order][j + 1].buddy_id;
                }
                free_table.stack_top[current_order]--;
                
                // 准备下一次合并
                current_page = (current_page < buddy_page) ? current_page : buddy_page;
                current_order++;
                break;
            }
        }
        
        if (!found_buddy) break;  // 没有伙伴，停止合并
    }
    
    // 将合并后的块加入空闲表
    free_table.stack_top[current_order]++;
    free_table.table[current_order][free_table.stack_top[current_order]].page_index_start = current_page;
    free_table.table[current_order][free_table.stack_top[current_order]].buddy_id = current_buddy_id;
}
```
1. 从分配表中找到要释放的页面。
2. 从分配表中移除该块（栈式操作）。
3. 释放后，尝试将相邻的伙伴块合并成更大的块。合并时，使用异或操作`current_page ^ (1 << current_order)`来计算伙伴块的地址，并查找该伙伴块是否为空闲。
4. 继续合并，直到无法合并为止。
5. 最终，将合并后的块加入空闲表中。
## 修改方案
### 使用链表代替栈结构
* **更灵活的内存管理**  
栈操作中，插入或删除元素只能在栈顶进行，导致需要进行额外的内存移动操作。  
双向链表允许从两端插入和删除元素，并且每个元素都有指向前后元素的指针，避免了栈结构中常见的元素移动。
* **高效伙伴合并操作**  
合并两个伙伴块时，双向链表能够直接修改元素的指针，而不需要重新排列整个表
* **双向链表的实现**  
`buddy_free_node`和`buddy_occu_node`结构体中增加了`prev`和`next`指针来实现双向链表。链表节点的插入和删除操作通过修改这些指针来实现。
* **插入和删除操作**  
在`get_page_buddy`和`free_buddy_page函数中，插入和删除节点操作通过链表指针操作来实现，从而避免了栈结构中元素的移动。
### 压缩伙伴ID
* **位图存储结构**  
使用一个整型数组`unsigned long bitmap[MAX_PAGE_NUM / 64]`来存储位图。每个`unsigned long`类型可以存储 64 位的信息，因此每个数组元素对应 64 个物理页的状态。
* **位图管理**  
使用`buddy_bitmap`代替了`buddy_id`数组，`buddy_bitmap`是一个位图，每个物理页的分配状态用一个位来表示。  
通过`set_bit`,`clear_bit`和`test_bit`来操作位图，标记和检查内存页是否已分配。
* **初始化修改**  
初始化时将物理页位图的每一位清零，并在`init_buddy`中将整个内存页标记为已分配。
* **分配和释放修改**  
在分配内存时,`set_bit`用来标记物理页已分配。释放时，`clear_bit`用来标记物理页为未分配状态。