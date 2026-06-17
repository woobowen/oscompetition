#include "mod.h"

static buffer_node_t buf_cache[N_BUFFER];
static buffer_node_t buf_head_active, buf_head_inactive;
static spinlock_t lk_buf_cache;
static bool buf_cache_ready;

// 优化：哈希表索引（链式哈希，桶数组 + 每节点的哈希next）
#define BUFFER_HASH_SIZE 61
#define BUFFER_RECLAIM_ON_OOM 512
static buffer_node_t *buf_hash[BUFFER_HASH_SIZE];
static buffer_node_t *hash_next[N_BUFFER];

static inline uint32 hash_func(uint32 block_num) { return block_num % BUFFER_HASH_SIZE; }
static inline int node_index(buffer_node_t *node) { return (int)(node - buf_cache); }

// 在哈希表插入/删除节点
static void hash_insert(buffer_node_t *node) {
	assert(node->buf.block_num != BLOCK_NUM_UNUSED, "hash_insert: invalid block");
	uint32 h = hash_func(node->buf.block_num);
	hash_next[node_index(node)] = buf_hash[h];
	buf_hash[h] = node;
}
static void hash_remove(buffer_node_t *node) {
	if (node->buf.block_num == BLOCK_NUM_UNUSED) return;
	uint32 h = hash_func(node->buf.block_num);
	buffer_node_t **pp = &buf_hash[h];
	while (*pp) {
		if (*pp == node) { *pp = hash_next[node_index(node)]; break; }
		pp = &hash_next[node_index(*pp)];
	}
	hash_next[node_index(node)] = NULL;
}
// 通过哈希查找块，返回节点（或NULL）
static buffer_node_t* hash_find(uint32 block_num) {
	if (block_num == BLOCK_NUM_UNUSED) return NULL;
	uint32 h = hash_func(block_num);
	for (buffer_node_t *n = buf_hash[h]; n; n = hash_next[node_index(n)]) {
		if (n->buf.block_num == block_num) return n;
	}
	return NULL;
}

static uint32 buffer_reclaim_locked(uint32 buffer_count, buffer_node_t *skip)
{
	uint32 freed = 0;
	for (buffer_node_t *node = buf_head_inactive.prev; node != &buf_head_inactive && freed < buffer_count; node = node->prev) {
		if (node == skip)
			continue;
		if (node->buf.ref == 0 && node->buf.data != NULL) {
			pmem_free((uint64)node->buf.data, true);
			node->buf.data = NULL;
			node->buf.valid = false;
			hash_remove(node);
			node->buf.block_num = BLOCK_NUM_UNUSED;
			freed++;
		}
	}
	return freed;
}

/* 
	将一个节点拿出来并插入
	1. 活跃链表的头部 buf_head_active->next
	2. 活跃链表的尾部 buf_head_active->prev
	3. 不活跃链表的头部 buf_head_inactive->next
	4. 不活跃链表的尾部 buf_head_inactive->prev
*/
static void insert_node(buffer_node_t *node, bool insert_active, bool insert_next)
{
	/* 如果有需要, 让node先离开当前位置 */
	if (node->next != NULL && node->prev != NULL) {
		node->next->prev = node->prev;
		node->prev->next = node->next;
	}

	/* 选择目标双向循环链表 */
	buffer_node_t *head = &buf_head_inactive;
	if (insert_active)
		head = &buf_head_active;

	/* 然后将node插入head->next or head->prev */	
	if (insert_next) {
		node->next = head->next;
		node->next->prev = node;
		node->prev = head;
		head->next = node;
	} else {
		node->prev = head->prev;
		node->prev->next = node;
		node->next = head;
		head->prev = node;
	}
}

/* 
	buffer系统初始化：
	1. 初始化全局的lk_buf_cache + buf_head_active + buf_head_inactive
	2. 初始化buf_cache中的所有node, 并将他们放在不活跃链表中
*/
void buffer_init()
{
	buf_cache_ready = false;

	// 初始化两个链表头为自环
	buf_head_active.next = &buf_head_active;
	buf_head_active.prev = &buf_head_active;
	buf_head_inactive.next = &buf_head_inactive;
	buf_head_inactive.prev = &buf_head_inactive;
	spinlock_init(&lk_buf_cache, "buffer_cache");

	// 清空哈希桶
	for (int i = 0; i < BUFFER_HASH_SIZE; i++) buf_hash[i] = NULL;
	for (int i = 0; i < (int)N_BUFFER; i++) hash_next[i] = NULL;

	// 初始化所有缓存节点，放入不活跃链表。
	for (int i = 0; i < (int)N_BUFFER; i++) {
		buffer_node_t *node = &buf_cache[i];
		node->buf.block_num = BLOCK_NUM_UNUSED;
		node->buf.ref = 0;
		node->buf.data = NULL;
		node->buf.disk = false;
		node->buf.valid = false;
		sleeplock_init(&node->buf.slk, "buffer");
		insert_node(node, /*active*/false, /*insert_next*/true);
	}
	buf_cache_ready = true;
}

/* 磁盘读取: block -> buf */
static void buffer_read(buffer_t *buf)
{
	// 睡眠锁检查
	assert(sleeplock_holding(&buf->slk), "buffer_read: sleeplock not held");
	virtio_disk_rw(buf, /*write*/false);
}

/* 磁盘写入: buf -> block */
void buffer_write(buffer_t *buf)
{
	assert(sleeplock_holding(&buf->slk), "buffer_write: sleeplock not held");
	virtio_disk_rw(buf, /*write*/true);
}

/* 从buf_cache中获取一个buf */
buffer_t* buffer_get(uint32 block_num)
{
	spinlock_acquire(&lk_buf_cache);

	// 优化：直接用哈希查找
	buffer_node_t *node = hash_find(block_num);
	// 1) 命中：移动到活跃链表头部
	if (node) {
		insert_node(node, /*active*/true, /*insert_next*/true);
		node->buf.ref++;
		if (node->buf.data == NULL) {  // 检查是否需要补页
			uint64 pa = (uint64)pmem_alloc(true);
			if (pa == 0) {
				buffer_reclaim_locked(BUFFER_RECLAIM_ON_OOM, node);
				pa = (uint64)pmem_alloc(true);
			}
			assert(pa != 0, "buffer_get: pmem_alloc failed");
			node->buf.data = (uint8*)pa;
			node->buf.valid = false;
		}
		spinlock_release(&lk_buf_cache);
		sleeplock_acquire(&node->buf.slk);
		if (!node->buf.valid) {
			buffer_read(&node->buf);
			node->buf.valid = true;
		}
		return &node->buf;
	}
	// 2) 未命中：选择不活跃链表中最不活跃的替换
	buffer_node_t *victim = buf_head_inactive.prev;
	assert(victim != &buf_head_inactive, "buffer_get: no inactive buffer available");
	assert(victim->buf.ref == 0, "buffer_get: victim ref not zero");

	// 如无物理页则分配
	if (victim->buf.data == NULL) {
		uint64 pa = (uint64)pmem_alloc(true);
		if (pa == 0) {
			buffer_reclaim_locked(BUFFER_RECLAIM_ON_OOM, victim);
			pa = (uint64)pmem_alloc(true);
		}
		assert(pa != 0, "buffer_get: pmem_alloc failed (victim)");
		victim->buf.data = (uint8*)pa;
	}

	// 维护哈希：移除旧映射，插入新映射
	hash_remove(victim);
	victim->buf.block_num = block_num;
	victim->buf.valid = false;
	hash_insert(victim);

	// 移动到活跃链表尾并增加引用
	insert_node(victim, /*active*/true, /*insert_next*/false);
	victim->buf.ref++;
	spinlock_release(&lk_buf_cache);

	// 加锁并进行磁盘读取
	sleeplock_acquire(&victim->buf.slk);
	if (!victim->buf.valid) {
		buffer_read(&victim->buf);
		victim->buf.valid = true;
	}
	return &victim->buf;
}

/* 向buf_cache归还一个buf */
void buffer_put(buffer_t *buf)
{
	// 释放内部睡眠锁
	if (sleeplock_holding(&buf->slk))
		sleeplock_release(&buf->slk);

	spinlock_acquire(&lk_buf_cache);
	assert(buf->ref > 0, "buffer_put: ref already zero");
	buf->ref--;
	if (buf->ref == 0) { 
		buffer_node_t *node = (buffer_node_t *)buf; // 找到对应的节点
		insert_node(node, /*active*/false, /*insert_next*/true); // 移入不活跃链表头部
	}
	spinlock_release(&lk_buf_cache);
}

/*
	从后向前遍历非活跃链表, 尝试释放buffer_count个buffer持有的物理内存(data)
	返回成功释放资源的buffer数量
*/
uint32 buffer_freemem(uint32 buffer_count)
{
	if (!buf_cache_ready)
		return 0;

	spinlock_acquire(&lk_buf_cache);
	uint32 freed = buffer_reclaim_locked(buffer_count, NULL);
	spinlock_release(&lk_buf_cache);
	return freed;
}

/* 输出buffer_cache的信息 (for test) */
void buffer_print_info()
{
	buffer_node_t *node;

	assert(N_BUFFER == N_BUFFER_TEST, "buffer_print_info: invalid N_BUFFER");

	spinlock_acquire(&lk_buf_cache);

	printf("buffer_cache information:\n");
	
	printf("1.active list:\n");
	for (node = buf_head_active.next; node != &buf_head_active; node = node->next) {
		printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
			(int)(node - buf_cache), node->buf.ref, (uint64)node->buf.data, node->buf.block_num);
	}
	printf("over!\n");

	printf("2.inactive list:\n");
	for (node = buf_head_inactive.next; node != &buf_head_inactive; node = node->next) {
		printf("buffer %d(ref = %d): page(pa = %p) -> block[%d]\n",
			(int)(node - buf_cache), node->buf.ref, (uint64)node->buf.data, node->buf.block_num);
	}
	printf("over!\n");

	spinlock_release(&lk_buf_cache);
}
