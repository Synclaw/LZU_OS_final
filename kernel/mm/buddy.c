#define MAX_ORDER 15
#define MAX_PAGE_NUM  32768 // 如果太大可以减小，定义物理页面的最大数量

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
typedef struct buddy_free_node
{
    int page_index_start; // 空闲块起始页的索引
} buddy_free_node;

// 空闲表结构，表示所有的空闲块，按阶数分组
typedef struct buddy_free_table
{
    /* 
    空闲表由多个链表组成，按阶数（block size的对数）分组。
    第一维表示阶数。
    第二维表示该阶数下的空闲块链表。
    由于无法使用malloc，我们静态分配全部空间并将其当作一个栈使用：
    - 新增空闲块时，入栈；
    - 分配时，出栈。
    */
    buddy_free_node table[MAX_ORDER + 1][MAX_PAGE_NUM];
    // 每个阶数对应的栈顶指针
    int stack_top[MAX_ORDER + 1];
} buddy_free_table;

// 分配表节点，记录已分配块的信息
typedef struct buddy_occu_node
{
    int page_index_start; // 已分配块的起始页索引
} buddy_occu_node;

// 分配表结构，记录已分配的块信息
typedef struct buddy_occu_table
{
    // 记录每个阶数下已分配的块信息，类似空闲表的结构
    buddy_occu_node table[MAX_ORDER][MAX_PAGE_NUM];
    // 每个阶数对应的栈顶指针
    int stack_top[MAX_ORDER + 1];
} buddy_occu_table;

// 空闲表和分配表的实例
static buddy_free_table free_table;
static buddy_occu_table occu_table;

// 初始化伙伴系统
// 初始化空闲表、分配表和位图
void init_buddy() {
    // 初始化位图，所有位都清零，表示物理页未分配
    for (int i = 0; i < MAX_PAGE_NUM / 64; i++) {
        buddy_bitmap[i] = 0;
    }
    
    // 初始化空闲表，设置每个阶数的栈顶为-1，表示栈为空
    for (int i = 0; i <= MAX_ORDER; i++) {
        free_table.stack_top[i] = -1;
    }
    
    // 初始化分配表，设置每个阶数的栈顶为-1，表示栈为空
    for (int i = 0; i <= MAX_ORDER; i++) {
        occu_table.stack_top[i] = -1;
    }
    
    // 初始时，将整个内存作为一个大块放入最高阶的空闲表中
    free_table.stack_top[MAX_ORDER] = 0;
    free_table.table[MAX_ORDER][0].page_index_start = 0;
    
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
        if (free_table.stack_top[current_order] >= 0) {
            // 找到可用的空闲块
            int stack_index = free_table.stack_top[current_order];
            int page_start = free_table.table[current_order][stack_index].page_index_start;
            
            // 从空闲表移除该块（栈操作）
            free_table.stack_top[current_order]--;
            
            // 将块添加到分配表中
            occu_table.stack_top[order]++;
            occu_table.table[order][occu_table.stack_top[order]].page_index_start = page_start;
            
            // 如果块太大，需要进行分割
            while (current_order > order) {
                current_order--;
                int buddy_page = page_start + (1 << current_order);
                
                // 将分割出的伙伴块加入空闲表
                free_table.stack_top[current_order]++;
                free_table.table[current_order][free_table.stack_top[current_order]].page_index_start = buddy_page;
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
// page为释放的物理页的起始地址（即在mem_map数组中的索引）
void free_buddy_page(int page) {
    // 查找要释放的页面在分配表中的位置
    int found_order = -1;
    int found_index = -1;
    
    for (int order = 0; order <= MAX_ORDER; order++) {
        for (int i = 0; i <= occu_table.stack_top[order]; i++) {
            if (occu_table.table[order][i].page_index_start == page) {
                found_order = order;
                found_index = i;
                break;
            }
        }
        if (found_order != -1) break;  // 找到目标页面
    }
    
    if (found_order == -1) return;  // 页面未被分配，释放失败
    
    // 从分配表中移除该块（栈操作）
    for (int i = found_index; i < occu_table.stack_top[found_order]; i++) {
        // 手动复制数组元素
        occu_table.table[found_order][i].page_index_start = occu_table.table[found_order][i + 1].page_index_start;
    }
    occu_table.stack_top[found_order]--;
    
    // 标记该页面为未分配
    clear_bit(page);
    
    // 释放后尝试能否合并相邻伙伴块
    int current_order = found_order;
    int current_page = page;
    
    while (current_order < MAX_ORDER) {
        int buddy_page = current_page ^ (1 << current_order);  // 计算伙伴块的地址
        int found_buddy = 0;
        
        // 在空闲表中查找伙伴块
        for (int i = 0; i <= free_table.stack_top[current_order]; i++) {
            if (free_table.table[current_order][i].page_index_start == buddy_page) {
                // 找到伙伴块，可以合并
                found_buddy = 1;
                
                // 从空闲表中移除伙伴块（栈操作）
                for (int j = i; j < free_table.stack_top[current_order]; j++) {
                    // 手动复制数组元素
                    free_table.table[current_order][j].page_index_start = free_table.table[current_order][j + 1].page_index_start;
                }
                free_table.stack_top[current_order]--;
                
                // 准备下一次合并
                current_page = (current_page < buddy_page) ? current_page : buddy_page;
                current_order++;
                break;
            }
        }
        
        if (!found_buddy) break;  // 没有找到伙伴，停止合并
    }
    
    // 将合并后的块加入空闲表
    free_table.stack_top[current_order]++;
    free_table.table[current_order][free_table.stack_top[current_order]].page_index_start = current_page;
}
