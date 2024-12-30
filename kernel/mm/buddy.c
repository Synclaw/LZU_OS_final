#define NULL 0
#define MAX_ORDER 15
#define MAX_PAGE_NUM  32768 // 如果太大可以减小，定义物理页面的最大数量

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
    int page_index_start; // 空闲块起始页的索引
    struct buddy_free_node *prev, *next; // 双向链表指针
} buddy_free_node;

// 空闲表结构，表示所有的空闲块，按阶数分组
typedef struct buddy_free_table {
    buddy_free_node *head[MAX_ORDER + 1]; // 双向链表的头指针
    buddy_free_node *tail[MAX_ORDER + 1]; // 双向链表的尾指针
} buddy_free_table;

// 分配表节点，记录已分配块的信息
typedef struct buddy_occu_node {
    int page_index_start; // 已分配块的起始页索引
    struct buddy_occu_node *prev, *next; // 双向链表指针
} buddy_occu_node;

// 分配表结构，记录已分配的块信息
typedef struct buddy_occu_table {
    buddy_occu_node *head[MAX_ORDER]; // 双向链表的头指针
    buddy_occu_node *tail[MAX_ORDER]; // 双向链表的尾指针
} buddy_occu_table;

// 空闲表和分配表的实例
static buddy_free_table free_table;
static buddy_occu_table occu_table;

// 手动分配内存区域，模拟 `malloc`
// 分配一个指定大小的内存块，返回该块的指针
void* buddy_malloc(size_t size) {
    static unsigned char memory_pool[MAX_PAGE_NUM * 4096]; // 每个页大小为 4096 字节
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
    new_node->prev = NULL;
    new_node->next = NULL;
    free_table.head[MAX_ORDER] = free_table.tail[MAX_ORDER] = new_node;

    // 将整个物理页标记为已分配
    set_bit(0);
}

// 计算所需的阶数（根据请求的大小）
static int calculate_order(int size) {
    int order = 0;
    size = size - 1;  // 减1是为了处理正好是2的幂的情况
    while (size > 0) {
        size >>= 1;
        order++;
    }
    return order;
}

// 获取所需大小的物理页，返回起始物理页地址，分配失败返回-1
int get_page_buddy(int size) {
    if (size <= 0 || size > (1 << MAX_ORDER)) {
        return -1;  // 请求的大小无效
    }

    // 根据请求的大小计算需要的阶数
    int order = calculate_order(size);
    int current_order = order;

    // 查找合适的空闲块
    while (current_order <= MAX_ORDER) {
        if (free_table.head[current_order] != NULL) {
            // 找到可用的空闲块
            buddy_free_node *node = free_table.head[current_order];
            int page_start = node->page_index_start;

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
            occu_node->page_index_start = page_start;
            occu_node->prev = NULL;
            occu_node->next = occu_table.head[order];
            if (occu_table.head[order] != NULL) {
                occu_table.head[order]->prev = occu_node;
            }
            occu_table.head[order] = occu_node;
            if (occu_table.tail[order] == NULL) {
                occu_table.tail[order] = occu_node;
            }

            // 如果块太大，需要进行分割
            while (current_order > order) {
                current_order--;
                int buddy_page = page_start + (1 << current_order);

                // 将分割出的伙伴块加入空闲表
                buddy_free_node *new_free_node = (buddy_free_node*)buddy_malloc(sizeof(buddy_free_node));
                new_free_node->page_index_start = buddy_page;
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
            set_bit(page_start);

            return page_start;
        }
        current_order++;
    }

    return -1;  // 无法找到合适的空闲块
}

// 释放已分配的物理页
void free_buddy_page(int page) {
    // 查找要释放的页面在分配表中的位置
    int found_order = -1;
    buddy_occu_node *found_node = NULL;

    for (int order = 0; order < MAX_ORDER; order++) {
        for (buddy_occu_node *node = occu_table.head[order]; node != NULL; node = node->next) {
            if (node->page_index_start == page) {
                found_order = order;
                found_node = node;
                break;
            }
        }
        if (found_order != -1) break;
    }

    if (found_order == -1) return;  // 页面未被分配，释放失败

    // 从分配表中移除该块
    if (occu_table.head[found_order] == found_node) {
        occu_table.head[found_order] = found_node->next;
    }
    if (occu_table.tail[found_order] == found_node) {
        occu_table.tail[found_order] = found_node->prev;
    }
    if (found_node->prev != NULL) {
        found_node->prev->next = found_node->next;
    }
    if (found_node->next != NULL) {
        found_node->next->prev = found_node->prev;
    }

    // 标记该页面为未分配
    clear_bit(page);

    // 释放后尝试能否合并相邻伙伴块
    int current_order = found_order;
    int current_page = page;

    while (current_order < MAX_ORDER) {
        int buddy_page = current_page ^ (1 << current_order);  // 计算伙伴块的地址
        int found_buddy = 0;

        // 在空闲表中查找伙伴块
        for (buddy_free_node *node = free_table.head[current_order]; node != NULL; node = node->next) {
            if (node->page_index_start == buddy_page) {
                // 找到伙伴块，可以合并
                found_buddy = 1;

                // 从空闲表中移除伙伴块
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

                // 准备下一次合并
                current_page = (current_page < buddy_page) ? current_page : buddy_page;
                current_order++;
                break;
            }
        }

        if (!found_buddy) break;  // 没有找到伙伴，停止合并
    }

    // 将合并后的块加入空闲表
    buddy_free_node *new_node = (buddy_free_node*)buddy_malloc(sizeof(buddy_free_node));
    new_node->page_index_start = current_page;
    new_node->prev = NULL;
    new_node->next = free_table.head[current_order];
    if (free_table.head[current_order] != NULL) {
        free_table.head[current_order]->prev = new_node;
    }
    free_table.head[current_order] = new_node;
    if (free_table.tail[current_order] == NULL) {
        free_table.tail[current_order] = new_node;
    }
}
