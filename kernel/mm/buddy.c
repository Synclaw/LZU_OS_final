#define NULL 0
#define MAX_ORDER 15
#define MAX_PAGE_NUM  32768  // 定义物理页面的最大数量
#define MAX_MEMORY_SIZE (MAX_PAGE_NUM * 4096) // 定义最大内存数量

typedef unsigned int size_t;

// 位图数组，表示每个物理页的分配状态
static unsigned long buddy_bitmap[MAX_PAGE_NUM / 64];

// 设置位图某一位为1
static inline void set_bit(int idx) {
    buddy_bitmap[idx / 64] |= (1UL << (idx % 64));
}

// 设置位图某一位为0
static inline void clear_bit(int idx) {
    buddy_bitmap[idx / 64] &= ~(1UL << (idx % 64));
}

// 检查位图某一位是否为1
static inline int test_bit(int idx) {
    return buddy_bitmap[idx / 64] & (1UL << (idx % 64));
}

// 空闲表节点，表示每个空闲块的信息
typedef struct buddy_free_node {
    int page_index_start;  // 空闲块起始页的索引
    int size;              // 块的大小（以物理页为单位）
    struct buddy_free_node *prev, *next;  // 双向链表指针
} buddy_free_node;

// 空闲表结构，表示所有的空闲块，按阶数分组
typedef struct buddy_free_table {
    buddy_free_node *head[MAX_ORDER + 1]; // 双向链表的头指针
    buddy_free_node *tail[MAX_ORDER + 1]; // 双向链表的尾指针
} buddy_free_table;

// 分配表节点，记录已分配块的信息
typedef struct buddy_occu_node {
    int page_index_start;  // 已分配块的起始页索引
    int size;              // 块的大小（以物理页为单位）
    struct buddy_occu_node *prev, *next;  // 双向链表指针
} buddy_occu_node;

// 分配表结构，记录已分配的块信息
typedef struct buddy_occu_table {
    buddy_occu_node *head[MAX_ORDER]; // 双向链表的头指针
    buddy_occu_node *tail[MAX_ORDER]; // 双向链表的尾指针
} buddy_occu_table;

// 进程内存块节点，记录每个进程的内存块地址和大小
typedef struct process_memory_node {
    int page_index_start;  // 块的起始页索引
    int size;              // 块的大小（以物理页为单位）
    struct process_memory_node *next; // 链表指针
} process_memory_node;

// 进程内存块链表，记录每个进程占用的内存块
typedef struct process_memory_table {
    process_memory_node *head; // 头指针
} process_memory_table;

// 空闲表和分配表的实例
static buddy_free_table free_table;
static buddy_occu_table occu_table;
// 定义一个进程内存表
static process_memory_table process_mem_table;

// 手动分配内存区域，模拟 `malloc`
void* buddy_malloc(size_t size) {
    static unsigned char memory_pool[MAX_MEMORY_SIZE]; // 每个页大小为 4096 字节
    static int next_free_byte = 0;  // 用于追踪下一次可用的内存区域
    
    if (next_free_byte + size > sizeof(memory_pool)) {
        return NULL;  // 内存不足
    }
    
    void* ptr = &memory_pool[next_free_byte];
    next_free_byte += size;
    return ptr;
}

// 手动释放内存区域，模拟 `free`
// 这里只做标记，不做实现
void buddy_free(void* ptr) {
    
}

// 初始化伙伴系统
void init_buddy() {
    // 初始化位图，所有位都清零，表示物理页未分配
    for (int i = 0; i < MAX_PAGE_NUM / 64; i++) {
        buddy_bitmap[i] = 0;
    }

    // 初始化空闲表，设置每个阶数的链表头尾为NULL，表示链表为空
    for (int i = 0; i <= MAX_ORDER; i++) {
        free_table.head[i] = NULL;
        free_table.tail[i] = NULL;
    }

    // 初始化分配表，设置每个阶数的链表头尾为NULL，表示链表为空
    for (int i = 0; i < MAX_ORDER; i++) {
        occu_table.head[i] = NULL;
        occu_table.tail[i] = NULL;
    }

    // 初始时，将整个内存作为一个大块放入最高阶的空闲表中
    buddy_free_node *new_node = (buddy_free_node*)buddy_malloc(sizeof(buddy_free_node));
    new_node->page_index_start = 0;
    new_node->size = MAX_PAGE_NUM;  // 整个内存块的大小
    new_node->prev = NULL;
    new_node->next = NULL;
    free_table.head[MAX_ORDER] = free_table.tail[MAX_ORDER] = new_node;

    // 将整个物理页标记为已分配
    set_bit(0);
}

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

// 释放进程所占用的内存
void free_buddy_page() {
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
