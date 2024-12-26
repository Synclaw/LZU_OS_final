#define MAX_ORDER 15
#define MAX_PAGE_NUM  32768 // 如果太大可以减小

// 伙伴id，互为伙伴的块从中取出一个id，并标记为使用
static int buddy_id[32768]; 

/* 后面的所有物理页地址都指的是mem_map数组中的索引 */

// 空闲表节点
typedef struct buddy_free_node
{
    int page_index_start; // 空闲块起始地址
    int buddy_id;         // 伙伴id
} buddy_free_node;

// 空闲表
typedef struct buddy_free_table
{
    /* 
    分为两个域。
    第一维表示阶数。
    第二维应该是链表，由于不能使用malloc，故静态分配全部空间。将其当作一个stack使用 
    新增空闲块则入栈，分配则出栈 
    */
    buddy_free_node table[MAX_ORDER + 1][MAX_PAGE_NUM];
    // 每阶对应的栈指针
    int stack_top[MAX_ORDER + 1];
} buddy_free_table;

typedef struct buddy_occu_node
{
    int page_index_start;
    int buddy_id;
} buddy_occu_node;

// 分配表
typedef struct buddy_occu_table
{
    // 含义同理
    buddy_occu_node table[MAX_ORDER][MAX_PAGE_NUM];
    int stack_top[MAX_ORDER + 1];
} buddy_occu_table;

// 空闲表与分配表
static buddy_free_table free_table;
static buddy_occu_table occu_table;


// 初始化伙伴系统
// 比如空闲表和分配表、伙伴id数组
void init_buddy() {
    // 初始化伙伴ID数组
    for (int i = 0; i < MAX_PAGE_NUM; i++) {
        buddy_id[i] = i;  // 每个页面初始化时有唯一的ID，从0开始
    }
    
    // 初始化空闲表
    for (int i = 0; i <= MAX_ORDER; i++) {
        free_table.stack_top[i] = -1;  // -1表示空栈
    }
    
    // 初始化分配表
    for (int i = 0; i <= MAX_ORDER; i++) {
        occu_table.stack_top[i] = -1; //同上
    }
    
    // 初始时，将所有内存作为一个大块放入最高阶的空闲表
    free_table.stack_top[MAX_ORDER] = 0;
    free_table.table[MAX_ORDER][0].page_index_start = 0;
    free_table.table[MAX_ORDER][0].buddy_id = buddy_id[0];
}

// 计算所需的阶数
static int calculate_order(int size) {
    int order = 0;
    size = size - 1;  // 减1是为了处理正好是2的幂的情况
    while (size > 0) {
        size >>= 1;
        order++;
    }
    return order;
}

// size 代表需要的物理页数量
// 分配失败返回-1
int get_page_buddy(int size) {
    if (size <= 0 || size > (1 << MAX_ORDER)) {
        return -1;
    }
    
    int order = calculate_order(size);
    int current_order = order;
    
    // 查找合适的空闲块
    while (current_order <= MAX_ORDER) {
        if (free_table.stack_top[current_order] >= 0) {
            // 找到可用块
            int stack_index = free_table.stack_top[current_order];
            int page_start = free_table.table[current_order][stack_index].page_index_start;
            int current_buddy_id = free_table.table[current_order][stack_index].buddy_id;
            
            // 从空闲表移除
            free_table.stack_top[current_order]--;
            
            // 将块添加到分配表
            occu_table.stack_top[order]++;
            occu_table.table[order][occu_table.stack_top[order]].page_index_start = page_start;
            occu_table.table[order][occu_table.stack_top[order]].buddy_id = current_buddy_id;
            
            // 如果块太大，需要分割
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

    return -1;
}

// page 代表起始物理页地址，即在mem_map数组中物理页的索引
// 成功返回0, 失败-1
void free_buddy_page(int page) {
    // 在分配表中查找页面
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
    
    if (found_order == -1) return;  // 页面还未被分配，失败
    
    // 从分配表中移除
    int current_buddy_id = occu_table.table[found_order][found_index].buddy_id;
    for (int i = found_index; i < occu_table.stack_top[found_order]; i++) {
        // 手动复制数组元素
        occu_table.table[found_order][i].page_index_start = occu_table.table[found_order][i + 1].page_index_start;
        occu_table.table[found_order][i].buddy_id = occu_table.table[found_order][i + 1].buddy_id;
    }
    occu_table.stack_top[found_order]--;
    
    // 释放后尝试能否合并
    int current_order = found_order;
    int current_page = page;
    
    while (current_order < MAX_ORDER) {
        int buddy_page = current_page ^ (1 << current_order);
        // 用异或操作 当前页面地址 XOR (1 << current_order)得到伙伴页面地址
        int found_buddy = 0;
        
        // 在空闲表中查找伙伴
        for (int i = 0; i <= free_table.stack_top[current_order]; i++) {
            if (free_table.table[current_order][i].page_index_start == buddy_page) {
                // 找到伙伴，可以合并
                found_buddy = 1;
                
                // 从空闲表中移除伙伴
                for (int j = i; j < free_table.stack_top[current_order]; j++) {
                    // 手动复制数组元素
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
        
        if (!found_buddy) break;
    }
    
    // 将最终合并块加入空闲表
    free_table.stack_top[current_order]++;
    free_table.table[current_order][free_table.stack_top[current_order]].page_index_start = current_page;
    free_table.table[current_order][free_table.stack_top[current_order]].buddy_id = current_buddy_id;
}