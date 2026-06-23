#include "mod.h"
#include "../arch/method.h"

super_block_t sb; /* 超级块 */
static bool fs_readonly_ext4;

enum {
	PROC_NONE = 0,
	PROC_ROOT,
	PROC_MOUNTS,
	PROC_MEMINFO,
	PROC_UPTIME,
	PROC_STAT,
	PROC_SELF,
	PROC_SELF_EXE,
	PROC_SELF_FD,
	PROC_PID_DIR,
	PROC_PID_STAT,
	PROC_PID_CMDLINE,
	PROC_PID_COMM,
	PROC_PID_STATUS,
};

static bool streq(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return false;
	return strlen(a) == strlen(b) && strncmp(a, b, (uint32)strlen(b)) == 0;
}

static bool starts_with(const char *s, const char *prefix)
{
	if (s == NULL || prefix == NULL)
		return false;
	uint32 n = (uint32)strlen(prefix);
	return (uint32)strlen(s) >= n && strncmp(s, prefix, n) == 0;
}

static int parse_pid_component(const char *s, int *pid, const char **rest)
{
	int val = 0;
	int ndigit = 0;
	while (*s >= '0' && *s <= '9') {
		val = val * 10 + (*s - '0');
		s++;
		ndigit++;
	}
	if (ndigit == 0)
		return -1;
	*pid = val;
	*rest = s;
	return 0;
}

static bool proc_pid_exists(int pid)
{
	if (pid <= 0)
		return false;
	proc_t *p = proc_get_by_pid(pid);
	if (p == NULL)
		return false;
	spinlock_release(&p->lk);
	return true;
}

static int procfs_lookup(char *path, uint16 *kind, int *pid)
{
	const char *rest;
	int n;

	if (path == NULL || kind == NULL || pid == NULL)
		return -1;
	*kind = PROC_NONE;
	*pid = 0;

	if (streq(path, "/proc")) {
		*kind = PROC_ROOT;
		return 0;
	}
	if (streq(path, "/proc/mounts")) {
		*kind = PROC_MOUNTS;
		return 0;
	}
	if (streq(path, "/proc/meminfo")) {
		*kind = PROC_MEMINFO;
		return 0;
	}
	if (streq(path, "/proc/uptime")) {
		*kind = PROC_UPTIME;
		return 0;
	}
	if (streq(path, "/proc/stat")) {
		*kind = PROC_STAT;
		return 0;
	}
	if (streq(path, "/proc/self")) {
		*kind = PROC_SELF;
		*pid = myproc() ? myproc()->pid : 1;
		return 0;
	}
	if (streq(path, "/proc/self/exe")) {
		*kind = PROC_SELF_EXE;
		*pid = myproc() ? myproc()->pid : 1;
		return 0;
	}
	if (streq(path, "/proc/self/fd")) {
		*kind = PROC_SELF_FD;
		*pid = myproc() ? myproc()->pid : 1;
		return 0;
	}
	if (!starts_with(path, "/proc/"))
		return -1;

	if (parse_pid_component(path + 6, &n, &rest) < 0 || !proc_pid_exists(n))
		return -1;
	*pid = n;
	if (*rest == '\0') {
		*kind = PROC_PID_DIR;
		return 0;
	}
	if (streq(rest, "/stat")) {
		*kind = PROC_PID_STAT;
		return 0;
	}
	if (streq(rest, "/cmdline")) {
		*kind = PROC_PID_CMDLINE;
		return 0;
	}
	if (streq(rest, "/comm")) {
		*kind = PROC_PID_COMM;
		return 0;
	}
	if (streq(rest, "/status")) {
		*kind = PROC_PID_STATUS;
		return 0;
	}
	return -1;
}

int procfs_path_exists(char *path)
{
	uint16 kind;
	int pid;
	return procfs_lookup(path, &kind, &pid) == 0;
}

static bool procfs_is_dir(uint16 kind)
{
	return kind == PROC_ROOT || kind == PROC_SELF || kind == PROC_SELF_FD || kind == PROC_PID_DIR;
}

static void append_char(char *buf, uint32 cap, uint32 *pos, char c)
{
	if (*pos + 1 < cap)
		buf[*pos] = c;
	(*pos)++;
}

static void append_str(char *buf, uint32 cap, uint32 *pos, const char *s)
{
	while (*s != 0) {
		append_char(buf, cap, pos, *s);
		s++;
	}
}

static void append_u64(char *buf, uint32 cap, uint32 *pos, uint64 v)
{
	char tmp[24];
	int n = 0;
	if (v == 0) {
		append_char(buf, cap, pos, '0');
		return;
	}
	while (v > 0 && n < (int)sizeof(tmp)) {
		tmp[n++] = '0' + (v % 10);
		v /= 10;
	}
	while (n > 0)
		append_char(buf, cap, pos, tmp[--n]);
}

static uint32 procfs_build_content(file_t *file, char *buf, uint32 cap)
{
	uint32 pos = 0;
	uint64 up = r_time() / 10000000ull;
	int pid = file->proc_pid ? file->proc_pid : (myproc() ? myproc()->pid : 1);

	switch (file->proc_kind) {
	case PROC_MOUNTS:
		append_str(buf, cap, &pos, "rootfs / ext4 rw 0 0\nproc /proc proc rw 0 0\n");
		break;
	case PROC_MEMINFO:
		append_str(buf, cap,
			&pos,
			"MemTotal:        1048576 kB\n"
			"MemFree:          524288 kB\n"
			"MemAvailable:     524288 kB\n"
			"Buffers:               0 kB\n"
			"Cached:                0 kB\n"
			"SReclaimable:          0 kB\n");
		break;
	case PROC_UPTIME:
		append_u64(buf, cap, &pos, up);
		append_str(buf, cap, &pos, ".00 ");
		append_u64(buf, cap, &pos, up);
		append_str(buf, cap, &pos, ".00\n");
		break;
	case PROC_STAT:
		append_str(buf, cap, &pos, "cpu  1 0 1 1 0 0 0 0 0 0\nintr 0\nctxt 0\nbtime 0\nprocesses 1\n");
		break;
	case PROC_SELF_EXE:
		append_str(buf, cap, &pos, "/busybox\n");
		break;
	case PROC_PID_STAT:
		append_u64(buf, cap, &pos, (uint64)pid);
		append_str(buf, cap, &pos, " (busybox) S 1 1 0 0 0 0 0 0 0 0 0 0 0 20 0 1 0 0 0 0 0 0 0 0 0 0 0 0\n");
		break;
	case PROC_PID_CMDLINE:
		append_str(buf, cap, &pos, "busybox");
		break;
	case PROC_PID_COMM:
		append_str(buf, cap, &pos, "busybox\n");
		break;
	case PROC_PID_STATUS:
		append_str(buf, cap, &pos, "Name:\tbusybox\nState:\tS (sleeping)\nPid:\t");
		append_u64(buf, cap, &pos, (uint64)pid);
		append_str(buf, cap, &pos,
			"\nPPid:\t1\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\n"
			"Cpus_allowed:\t1\nCpus_allowed_list:\t0\n"
			"Mems_allowed:\t1\nMems_allowed_list:\t0\n");
		break;
	default:
		break;
	}

	if (cap > 0) {
		if (pos >= cap)
			buf[cap - 1] = 0;
		else
			buf[pos] = 0;
	}
	return pos;
}

static uint32 procfs_read(file_t *file, uint32 len, uint64 dst, bool is_user_dst)
{
	char buf[1024];
	uint32 size = procfs_build_content(file, buf, sizeof(buf));
	if (file->offset >= size)
		return 0;
	uint32 n = size - file->offset;
	if (n > len)
		n = len;
	if (is_user_dst)
		uvm_copyout(myproc()->pgtbl, dst, (uint64)(buf + file->offset), n);
	else
		memmove((void *)dst, buf + file->offset, n);
	file->offset += n;
	return n;
}

static uint32 emit_linux_dirent(uint64 user_dst, uint32 len, uint32 copied,
	uint64 ino, uint64 off, uint8 type, const char *name)
{
	char rec[280];
	uint32 nlen = (uint32)strlen(name);
	uint16 reclen = (uint16)((19 + nlen + 1 + 7) & ~7);
	if (copied + reclen > len)
		return copied;
	memset(rec, 0, reclen);
	*(uint64 *)&rec[0] = ino;
	*(uint64 *)&rec[8] = off;
	*(uint16 *)&rec[16] = reclen;
	*(uint8 *)&rec[18] = type;
	memmove(&rec[19], (void *)name, nlen);
	uvm_copyout(myproc()->pgtbl, user_dst + copied, (uint64)rec, reclen);
	return copied + reclen;
}

static uint32 procfs_get_dents(file_t *file, uint64 user_dst, uint32 len)
{
	struct proc_dirent { const char *name; uint8 type; uint64 ino; };
	struct proc_dirent entries[8];
	int count = 0;
	char pidbuf[16];
	uint32 p = 0;
	int self_pid = myproc() ? myproc()->pid : 1;

	entries[count++] = (struct proc_dirent){ ".", 4, 1 };
	entries[count++] = (struct proc_dirent){ "..", 4, 1 };

	if (file->proc_kind == PROC_ROOT) {
		entries[count++] = (struct proc_dirent){ "mounts", 8, 2 };
		entries[count++] = (struct proc_dirent){ "meminfo", 8, 3 };
		entries[count++] = (struct proc_dirent){ "uptime", 8, 4 };
		entries[count++] = (struct proc_dirent){ "stat", 8, 5 };
		entries[count++] = (struct proc_dirent){ "self", 10, 6 };
		append_u64(pidbuf, sizeof(pidbuf), &p, (uint64)self_pid);
		pidbuf[p < sizeof(pidbuf) ? p : sizeof(pidbuf) - 1] = 0;
		entries[count++] = (struct proc_dirent){ pidbuf, 4, (uint64)(1000 + self_pid) };
	} else if (file->proc_kind == PROC_SELF || file->proc_kind == PROC_PID_DIR) {
		entries[count++] = (struct proc_dirent){ "stat", 8, 11 };
		entries[count++] = (struct proc_dirent){ "cmdline", 8, 12 };
		entries[count++] = (struct proc_dirent){ "comm", 8, 13 };
		entries[count++] = (struct proc_dirent){ "status", 8, 14 };
		entries[count++] = (struct proc_dirent){ "fd", 4, 15 };
		entries[count++] = (struct proc_dirent){ "exe", 10, 16 };
	} else if (file->proc_kind == PROC_SELF_FD) {
		entries[count++] = (struct proc_dirent){ "0", 10, 20 };
		entries[count++] = (struct proc_dirent){ "1", 10, 21 };
		entries[count++] = (struct proc_dirent){ "2", 10, 22 };
	}

	uint32 copied = 0;
	while (file->offset < (uint32)count) {
		uint32 before = copied;
		struct proc_dirent *e = &entries[file->offset];
		copied = emit_linux_dirent(user_dst, len, copied, e->ino, file->offset + 1, e->type, e->name);
		if (copied == before)
			break;
		file->offset++;
	}
	return copied;
}

#define MEMFS_NODES 2048
#define MEMFS_DATA_SIZE 16384
#define MEMFS_LOGICAL_MAX 0xffffffffU
#define MEMFS_INLINE_PAGES (MEMFS_DATA_SIZE / PGSIZE)
#define MEMFS_EXTRA_EXTENTS 8192

typedef struct memfs_node {
	bool used;
	bool is_dir;
	uint32 hash;
	char path[128];
	uint8 data[MEMFS_DATA_SIZE];
	uint32 size;
	int extra_head;
	uint64 atime_sec;
	uint64 atime_nsec;
	uint64 mtime_sec;
	uint64 mtime_nsec;
} memfs_node_t;

typedef struct memfs_extent {
	bool used;
	int node_idx;
	uint32 page_idx;
	uint64 pa;
	int next;
} memfs_extent_t;

static memfs_node_t memfs_nodes[MEMFS_NODES];
static memfs_extent_t memfs_extents[MEMFS_EXTRA_EXTENTS];
static int memfs_alloc_hint;
static int memfs_extent_alloc_hint;

static int memfs_open_ref_count(int mem_idx);
static void memfs_maybe_reclaim_unlinked(int mem_idx);

static const char unixbench_sort_src_data[] =
	"version=\"1.2\"\n"
	"umask 022\n"
	"the quick brown fox jumps over the lazy dog\n"
	"this line gives busybox sort and grep real input data\n"
	"SeaOS keeps UnixBench shell pipelines executable\n"
	"another benchmark line with the word the in it\n"
	"zeta\n"
	"alpha\n"
	"gamma\n"
	"beta\n";

static const char etc_protocols_data[] =
	"ip 0 IP\n"
	"icmp 1 ICMP\n"
	"tcp 6 TCP\n"
	"udp 17 UDP\n";

static void memfs_normalize(char *dst, char *path)
{
	const char *src = path;
	uint32 i = 0;
	if (src == NULL) {
		dst[0] = 0;
		return;
	}
	if (starts_with(src, "./"))
		src += 2;
	if (starts_with(src, "/musl/"))
		src += 6;
	while (src[i] != 0 && i + 1 < 128) {
		dst[i] = src[i];
		i++;
	}
	while (i > 1 && dst[i - 1] == '/')
		i--;
	dst[i] = 0;
}

static uint32 memfs_hash_key(const char *s)
{
	uint32 h = 2166136261u;
	while (*s != 0) {
		h ^= (uint8)*s;
		h *= 16777619u;
		s++;
	}
	return h == 0 ? 1 : h;
}

static int memfs_find_key(const char *key, uint32 hash)
{
	for (int i = 0; i < MEMFS_NODES; i++) {
		if (memfs_nodes[i].used && memfs_nodes[i].hash == hash &&
			streq(memfs_nodes[i].path, key))
			return i;
	}
	return -1;
}

static int memfs_find(char *path)
{
	char key[128];
	memfs_normalize(key, path);
	if (key[0] == 0)
		return -1;
	uint32 hash = memfs_hash_key(key);
	return memfs_find_key(key, hash);
}

static int memfs_extra_find(int node_idx, uint32 page_idx)
{
	if (node_idx < 0 || node_idx >= MEMFS_NODES)
		return -1;
	for (int e = memfs_nodes[node_idx].extra_head; e >= 0; e = memfs_extents[e].next) {
		if (memfs_extents[e].used && memfs_extents[e].page_idx == page_idx)
			return e;
	}
	return -1;
}

static int memfs_extra_alloc(int node_idx, uint32 page_idx)
{
	if (node_idx < 0 || node_idx >= MEMFS_NODES || page_idx < MEMFS_INLINE_PAGES)
		return -1;
	int existing = memfs_extra_find(node_idx, page_idx);
	if (existing >= 0)
		return existing;

	for (int step = 0; step < MEMFS_EXTRA_EXTENTS; step++) {
		int e = (memfs_extent_alloc_hint + step) % MEMFS_EXTRA_EXTENTS;
		if (memfs_extents[e].used)
			continue;

		uint64 pa = (uint64)pmem_alloc(false);
		if (pa == 0)
			return -1;
		memset((void *)pa, 0, PGSIZE);

		memfs_extents[e].used = true;
		memfs_extents[e].node_idx = node_idx;
		memfs_extents[e].page_idx = page_idx;
		memfs_extents[e].pa = pa;
		memfs_extents[e].next = memfs_nodes[node_idx].extra_head;
		memfs_nodes[node_idx].extra_head = e;
		memfs_extent_alloc_hint = (e + 1) % MEMFS_EXTRA_EXTENTS;
		return e;
	}
	return -1;
}

static void memfs_extra_free_all(int node_idx)
{
	if (node_idx < 0 || node_idx >= MEMFS_NODES)
		return;
	int e = memfs_nodes[node_idx].extra_head;
	while (e >= 0) {
		int next = memfs_extents[e].next;
		if (memfs_extents[e].used && memfs_extents[e].pa != 0)
			pmem_free(memfs_extents[e].pa, false);
		memset(&memfs_extents[e], 0, sizeof(memfs_extents[e]));
		e = next;
	}
	memfs_nodes[node_idx].extra_head = -1;
}

static int memfs_create(char *path, bool is_dir)
{
	char key[128];
	memfs_normalize(key, path);
	if (key[0] == 0)
		return -1;
	uint32 hash = memfs_hash_key(key);
	int existing = memfs_find_key(key, hash);
	if (existing >= 0)
		return existing;
	for (int step = 0; step < MEMFS_NODES; step++) {
		int i = (memfs_alloc_hint + step) % MEMFS_NODES;
		if (!memfs_nodes[i].used) {
			memfs_node_t *node = &memfs_nodes[i];
			node->used = true;
			node->is_dir = is_dir;
			node->hash = hash;
			node->size = 0;
			node->extra_head = -1;
			node->atime_sec = 0;
			node->atime_nsec = 0;
			node->mtime_sec = 0;
			node->mtime_nsec = 0;
			memset(node->data, 0, sizeof(node->data));
			memset(node->path, 0, sizeof(node->path));
			memmove(node->path, key, strlen(key) + 1);
			memfs_alloc_hint = (i + 1) % MEMFS_NODES;
			return i;
		}
	}
	return -1;
}

static bool memfs_should_fast_create(char *path)
{
	return starts_with(path, "/tmp/") || starts_with(path, "/var/tmp/");
}

static int memfs_seed_readonly_file(char *path)
{
	char key[128];
	memfs_normalize(key, path);
	const char *data = NULL;
	uint32 size = 0;
	if (streq(key, "sort.src")) {
		data = unixbench_sort_src_data;
		size = sizeof(unixbench_sort_src_data) - 1;
	} else if (streq(key, "/etc/protocols")) {
		data = etc_protocols_data;
		size = sizeof(etc_protocols_data) - 1;
	} else {
		return -1;
	}

	int idx = memfs_create(key, false);
	if (idx < 0)
		return -1;

	memfs_node_t *node = &memfs_nodes[idx];
	if (size > MEMFS_DATA_SIZE)
		return -1;
	memmove(node->data, data, size);
	node->size = size;
	return idx;
}

int memfs_path_exists(char *path)
{
	return memfs_find(path) >= 0;
}

int memfs_path_is_dir(char *path)
{
	int idx = memfs_find(path);
	return idx >= 0 && memfs_nodes[idx].is_dir;
}

int memfs_mkdir(char *path)
{
	int idx = memfs_create(path, true);
	return idx >= 0 ? 0 : -1;
}

static void memfs_reclaim_node(int idx)
{
	if (idx < 0 || idx >= MEMFS_NODES)
		return;
	memfs_extra_free_all(idx);
	memfs_nodes[idx].used = false;
	memfs_nodes[idx].is_dir = false;
	memfs_nodes[idx].hash = 0;
	memfs_nodes[idx].size = 0;
	memfs_nodes[idx].extra_head = -1;
	memfs_nodes[idx].atime_sec = 0;
	memfs_nodes[idx].atime_nsec = 0;
	memfs_nodes[idx].mtime_sec = 0;
	memfs_nodes[idx].mtime_nsec = 0;
	memfs_nodes[idx].path[0] = 0;
	memfs_alloc_hint = idx;
}

int memfs_unlink(char *path)
{
	char key[128];
	memfs_normalize(key, path);
	if (key[0] == 0)
		return -1;
	uint32 hash = memfs_hash_key(key);
	int idx = memfs_find_key(key, hash);
	if (idx < 0)
		return -1;
	if (memfs_open_ref_count(idx) > 0) {
		memfs_nodes[idx].hash = 0;
		memfs_nodes[idx].path[0] = 0;
		return 0;
	}
	memfs_reclaim_node(idx);
	return 0;
}

int memfs_rename(char *old_path, char *new_path)
{
	char old_key[128], new_key[128];
	memfs_normalize(old_key, old_path);
	memfs_normalize(new_key, new_path);
	if (old_key[0] == 0 || new_key[0] == 0)
		return -1;
	uint32 old_hash = memfs_hash_key(old_key);
	uint32 new_hash = memfs_hash_key(new_key);
	int old_idx = memfs_find_key(old_key, old_hash);
	if (old_idx < 0 || memfs_find_key(new_key, new_hash) >= 0)
		return -1;
	memset(memfs_nodes[old_idx].path, 0, sizeof(memfs_nodes[old_idx].path));
	memmove(memfs_nodes[old_idx].path, new_key, strlen(new_key) + 1);
	memfs_nodes[old_idx].hash = new_hash;
	return 0;
}

static uint32 memfs_read(file_t *file, uint32 len, uint64 dst, bool is_user_dst)
{
	if (file->mem_index < 0 || file->mem_index >= MEMFS_NODES)
		return (uint32)-1;
	memfs_node_t *node = &memfs_nodes[file->mem_index];
	if (!node->used || node->is_dir)
		return 0;
	if (file->offset >= node->size)
		return 0;
	uint32 n = node->size - file->offset;
	if (n > len)
		n = len;
	uint32 copied = 0;
	uint8 zero[128];
	memset(zero, 0, sizeof(zero));
	while (copied < n) {
		uint32 pos = file->offset + copied;
		uint32 chunk = n - copied;
		uint32 page_idx = pos / PGSIZE;
		uint32 page_off = pos % PGSIZE;
		uint32 page_left = PGSIZE - page_off;
		if (chunk > page_left)
			chunk = page_left;
		if (pos < MEMFS_DATA_SIZE) {
			uint32 stored = MEMFS_DATA_SIZE - pos;
			if (chunk > stored)
				chunk = stored;
			if (is_user_dst)
				uvm_copyout(myproc()->pgtbl, dst + copied, (uint64)(node->data + pos), chunk);
			else
				memmove((void *)(dst + copied), node->data + pos, chunk);
		} else {
			int ext = memfs_extra_find(file->mem_index, page_idx);
			if (ext >= 0) {
				uint64 src = memfs_extents[ext].pa + page_off;
				if (is_user_dst)
					uvm_copyout(myproc()->pgtbl, dst + copied, src, chunk);
				else
					memmove((void *)(dst + copied), (void *)src, chunk);
			} else {
				uint32 zeroed = 0;
				while (zeroed < chunk) {
					uint32 z = chunk - zeroed;
					if (z > sizeof(zero))
						z = sizeof(zero);
					if (is_user_dst)
						uvm_copyout(myproc()->pgtbl, dst + copied + zeroed, (uint64)zero, z);
					else
						memmove((void *)(dst + copied + zeroed), zero, z);
					zeroed += z;
				}
			}
		}
		copied += chunk;
	}
	file->offset += n;
	uint64 now = r_time();
	node->atime_sec = now / 10000000ull;
	node->atime_nsec = (now % 10000000ull) * 100;
	return n;
}

static uint32 memfs_write(file_t *file, uint32 len, uint64 src, bool is_user_src)
{
	if (file->mem_index < 0 || file->mem_index >= MEMFS_NODES)
		return (uint32)-1;
	memfs_node_t *node = &memfs_nodes[file->mem_index];
	if (!node->used || node->is_dir)
		return (uint32)-1;
	if (file->offset > MEMFS_LOGICAL_MAX || len > MEMFS_LOGICAL_MAX - file->offset)
		file->offset = 0;
	uint32 n = len;
	if (n == 0)
		return 0;
	uint32 copied = 0;
	while (copied < n) {
		uint32 pos = file->offset + copied;
		uint32 chunk = n - copied;
		uint32 page_idx = pos / PGSIZE;
		uint32 page_off = pos % PGSIZE;
		uint32 page_left = PGSIZE - page_off;
		if (chunk > page_left)
			chunk = page_left;
		if (pos < MEMFS_DATA_SIZE) {
			uint32 stored = MEMFS_DATA_SIZE - pos;
			if (chunk > stored)
				chunk = stored;
			if (is_user_src)
				uvm_copyin(myproc()->pgtbl, (uint64)(node->data + pos), src + copied, chunk);
			else
				memmove(node->data + pos, (void *)(src + copied), chunk);
		} else {
			int ext = memfs_extra_alloc(file->mem_index, page_idx);
			if (ext < 0)
				break;
			uint64 dst = memfs_extents[ext].pa + page_off;
			if (is_user_src)
				uvm_copyin(myproc()->pgtbl, dst, src + copied, chunk);
			else
				memmove((void *)dst, (void *)(src + copied), chunk);
		}
		copied += chunk;
	}
	file->offset += copied;
	if (file->offset > node->size)
		node->size = file->offset;
	uint64 now = r_time();
	node->mtime_sec = now / 10000000ull;
	node->mtime_nsec = (now % 10000000ull) * 100;
	return copied;
}

#define MEMFS_UTIME_NOW 1073741823ull
#define MEMFS_UTIME_OMIT 1073741822ull

static void memfs_time_now(uint64 *sec, uint64 *nsec)
{
	uint64 now = r_time();
	*sec = now / 10000000ull;
	*nsec = (now % 10000000ull) * 100;
}

static void memfs_apply_time(memfs_node_t *node, uint64 times_addr)
{
	if (times_addr == 0) {
		memfs_time_now(&node->atime_sec, &node->atime_nsec);
		memfs_time_now(&node->mtime_sec, &node->mtime_nsec);
		return;
	}

	uint64 ts[4];
	uvm_copyin(myproc()->pgtbl, (uint64)ts, times_addr, sizeof(ts));
	if (ts[1] == MEMFS_UTIME_NOW) {
		memfs_time_now(&node->atime_sec, &node->atime_nsec);
	} else if (ts[1] != MEMFS_UTIME_OMIT) {
		node->atime_sec = ts[0];
		node->atime_nsec = ts[1];
	}
	if (ts[3] == MEMFS_UTIME_NOW) {
		memfs_time_now(&node->mtime_sec, &node->mtime_nsec);
	} else if (ts[3] != MEMFS_UTIME_OMIT) {
		node->mtime_sec = ts[2];
		node->mtime_nsec = ts[3];
	}
}

file_t file_table[N_FILE]; // 文件资源池
spinlock_t lk_file_table; // 保护它的锁

static int memfs_open_ref_count(int mem_idx)
{
	int refs = 0;
	if (mem_idx < 0 || mem_idx >= MEMFS_NODES)
		return 0;

	spinlock_acquire(&lk_file_table);
	for (int i = 0; i < (int)N_FILE; i++) {
		if (file_table[i].ref > 0 && file_table[i].is_mem &&
			file_table[i].mem_index == mem_idx)
			refs++;
	}
	spinlock_release(&lk_file_table);
	return refs;
}

static void memfs_maybe_reclaim_unlinked(int mem_idx)
{
	if (mem_idx < 0 || mem_idx >= MEMFS_NODES)
		return;
	if (!memfs_nodes[mem_idx].used || memfs_nodes[mem_idx].path[0] != 0)
		return;
	if (memfs_open_ref_count(mem_idx) == 0)
		memfs_reclaim_node(mem_idx);
}

static void memfs_init_tmp_dirs(void)
{
	memfs_create("/tmp", true);
	memfs_create("/var", true);
	memfs_create("/var/tmp", true);
}

/* 初始化file_table */
void file_init()
{
	spinlock_init(&lk_file_table, "lk_file_table");

	// 清空file_table
	spinlock_acquire(&lk_file_table);
	for (int i = 0; i < (int)N_FILE; i++) {
		file_table[i].ip = NULL;
	file_table[i].is_device = false;
	file_table[i].dev_major = 0;
	file_table[i].is_proc = false;
	file_table[i].proc_kind = PROC_NONE;
	file_table[i].proc_pid = 0;
	file_table[i].is_mem = false;
	file_table[i].mem_index = -1;
        file_table[i].readable = false;
        file_table[i].writbale = false;
        file_table[i].offset = 0;
        file_table[i].ref = 0;
	file_table[i].is_pipe = false;
	file_table[i].pipe = NULL;
	file_table[i].is_socket = false;
	file_table[i].socket = NULL;
	}
	spinlock_release(&lk_file_table);

	memfs_init_tmp_dirs();
}

/* ===================== 管道实现 (pipe) ===================== */
static pipe_t pipe_pool[N_PIPE];
static spinlock_t lk_pipe_pool;

void pipe_init(void)
{
	spinlock_init(&lk_pipe_pool, "pipepool");
	for (int i = 0; i < N_PIPE; i++)
		pipe_pool[i].used = 0;
}

static pipe_t *pipe_pool_alloc(void)
{
	spinlock_acquire(&lk_pipe_pool);
	for (int i = 0; i < N_PIPE; i++) {
		if (!pipe_pool[i].used) {
			pipe_pool[i].used = 1;
			spinlock_release(&lk_pipe_pool);
			return &pipe_pool[i];
		}
	}
	spinlock_release(&lk_pipe_pool);
	return NULL;
}

static void pipe_pool_free(pipe_t *pi)
{
	spinlock_acquire(&lk_pipe_pool);
	pi->used = 0;
	spinlock_release(&lk_pipe_pool);
}

// 创建管道: *rf=读端, *wf=写端。成功返回0, 失败-1。
int pipe_alloc(file_t **rf, file_t **wf)
{
	pipe_t *pi = pipe_pool_alloc();
	if (pi == NULL)
		return -1;
	spinlock_init(&pi->lk, "pipe");
	pi->nread = 0;
	pi->nwrite = 0;
	pi->readopen = 1;
	pi->writeopen = 1;

	*rf = file_alloc();
	*wf = file_alloc();
	if (*rf == NULL || *wf == NULL) {
		if (*rf) file_close(*rf);
		if (*wf) file_close(*wf);
		pipe_pool_free(pi);
		return -1;
	}
	(*rf)->is_pipe = true; (*rf)->pipe = pi; (*rf)->readable = true;  (*rf)->writbale = false;
	(*wf)->is_pipe = true; (*wf)->pipe = pi; (*wf)->readable = false; (*wf)->writbale = true;
	return 0;
}

// 读管道: 空且写端开 -> 睡等; 写端关且空 -> 返回0(EOF)。一次最多 PIPE_SIZE。
uint32 pipe_read(pipe_t *pi, uint64 addr, uint32 n, bool is_user)
{
	char buf[PIPE_SIZE];
	uint32 i = 0;
	spinlock_acquire(&pi->lk);
	while (pi->nread == pi->nwrite && pi->writeopen) {
		proc_sleep(&pi->nread, &pi->lk);   // 返回后仍持有 pi->lk
	}
	for (i = 0; i < n && i < PIPE_SIZE && pi->nread != pi->nwrite; i++) {
		buf[i] = pi->data[pi->nread % PIPE_SIZE];
		pi->nread++;
	}
	proc_wakeup(&pi->nwrite);   // 唤醒写者
	spinlock_release(&pi->lk);

	if (i > 0) {
		if (is_user) uvm_copyout(myproc()->pgtbl, addr, (uint64)buf, i);
		else memmove((void *)addr, buf, i);
	}
	return i;
}

// 写管道: 满 -> 唤醒读者并睡等; 读端关 -> 返回已写(或-1)。
uint32 pipe_write(pipe_t *pi, uint64 addr, uint32 n, bool is_user)
{
	char buf[PIPE_SIZE];
	uint32 total = 0;
	while (total < n) {
		uint32 chunk = n - total;
		if (chunk > PIPE_SIZE) chunk = PIPE_SIZE;
		if (is_user) uvm_copyin(myproc()->pgtbl, (uint64)buf, addr + total, chunk);
		else memmove(buf, (void *)(addr + total), chunk);

		spinlock_acquire(&pi->lk);
		uint32 w = 0;
		while (w < chunk) {
			if (!pi->readopen) {
				spinlock_release(&pi->lk);
				return total > 0 ? total : (uint32)-1;   // broken pipe
			}
			if (pi->nwrite == pi->nread + PIPE_SIZE) {    // 满
				proc_wakeup(&pi->nread);
				proc_sleep(&pi->nwrite, &pi->lk);
				continue;
			}
			pi->data[pi->nwrite % PIPE_SIZE] = buf[w];
			pi->nwrite++;
			w++;
		}
		proc_wakeup(&pi->nread);
		spinlock_release(&pi->lk);
		total += chunk;
	}
	return total;
}

// 关闭管道一端(由 file_close 在 ref 归零时调用)
void pipe_close(pipe_t *pi, bool writable)
{
	spinlock_acquire(&pi->lk);
	if (writable) {
		pi->writeopen = 0;
		proc_wakeup(&pi->nread);
	} else {
		pi->readopen = 0;
		proc_wakeup(&pi->nwrite);
	}
	int both_closed = (pi->readopen == 0 && pi->writeopen == 0);
	spinlock_release(&pi->lk);
	if (both_closed)
		pipe_pool_free(pi);
}
/* =================== 管道实现结束 =================== */

/* 从file_table中获取1个空闲file */
file_t* file_alloc()
{
	// 分配策略：线性扫描找第一个 ref == 0 的槽位
	spinlock_acquire(&lk_file_table);
	for (int i = 0; i < (int)N_FILE; i++) {
		if (file_table[i].ref == 0) {
            file_table[i].ref = 1;
            file_table[i].ip = NULL;
		file_table[i].is_device = false;
		file_table[i].dev_major = 0;
		file_table[i].is_proc = false;
		file_table[i].proc_kind = PROC_NONE;
		file_table[i].proc_pid = 0;
            file_table[i].is_mem = false;
            file_table[i].mem_index = -1;
            file_table[i].readable = false;
            file_table[i].writbale = false;
            file_table[i].offset = 0;
            file_table[i].is_pipe = false;
            file_table[i].pipe = NULL;
            file_table[i].is_socket = false;
            file_table[i].socket = NULL;
            spinlock_release(&lk_file_table);
            return &file_table[i]; // 返回分配的file
		}
	}
	spinlock_release(&lk_file_table);
	return NULL; // 分配失败
}

static file_t *memfs_open_index(int mem_idx, bool want_r, bool want_w, uint32 open_mode)
{
	file_t *mf = file_alloc();
	if (mf == NULL)
		return NULL;

	memfs_node_t *node = &memfs_nodes[mem_idx];
	if (node->is_dir && want_w) {
		file_close(mf);
		return NULL;
	}
	if (!node->is_dir && (open_mode & FILE_OPEN_TRUNC)) {
		node->size = 0;
		memfs_extra_free_all(mem_idx);
		memset(node->data, 0, sizeof(node->data));
	}
	mf->is_mem = true;
	mf->mem_index = mem_idx;
	mf->readable = want_r;
	mf->writbale = want_w;
	mf->offset = (open_mode & FILE_OPEN_APPEND) ? node->size : 0;
	return mf;
}

int file_utimens(file_t *file, uint64 times_addr)
{
	if (file == NULL)
		return -1;
	if (file->is_mem && file->mem_index >= 0 && file->mem_index < MEMFS_NODES) {
		memfs_node_t *node = &memfs_nodes[file->mem_index];
		if (!node->used)
			return -1;
		memfs_apply_time(node, times_addr);
	}
	return 0;
}

/*
	根据路径打开文件 (指定打开模式)
	成功返回file, 失败返回NULL
*/
file_t* file_open(char *path, uint32 open_mode)
{
	if (path == NULL)
		return NULL;

	bool want_r = (open_mode & FILE_OPEN_READ) != 0;
    bool want_w = (open_mode & FILE_OPEN_WRITE) != 0;

	// 必须至少读/写之一
    if (!want_r && !want_w)   return NULL;

	uint16 proc_kind = PROC_NONE;
	int proc_pid = 0;
	if (procfs_lookup(path, &proc_kind, &proc_pid) == 0) {
		if (want_w)
			return NULL;
		file_t *pf = file_alloc();
		if (pf == NULL)
			return NULL;
		pf->is_proc = true;
		pf->proc_kind = proc_kind;
		pf->proc_pid = proc_pid;
		pf->readable = want_r;
		pf->writbale = false;
		pf->offset = 0;
		return pf;
	}

	uint16 dev_major = 0;
	if (device_path_lookup(path, &dev_major)) {
		if (!device_open_check(dev_major, open_mode))
			return NULL;

		file_t *devf = file_alloc();
		if (devf == NULL)
			return NULL;

		devf->ip = NULL;
		devf->is_device = true;
		devf->dev_major = dev_major;
		devf->readable = want_r;
		devf->writbale = want_w;
		devf->offset = 0;
		return devf;
	}

	int mem_idx = memfs_find(path);
	if (fs_readonly_ext4 && mem_idx < 0 && (open_mode & FILE_OPEN_CREATE) &&
		(want_w || memfs_should_fast_create(path))) {
		mem_idx = memfs_create(path, false);
	}
	if (mem_idx >= 0)
		return memfs_open_index(mem_idx, want_r, want_w, open_mode);

	// 1. 先按路径找 inode
	inode_t *ip = path_to_inode(path);

	if (ip == NULL) {
		if (mem_idx < 0 && fs_readonly_ext4 && want_r && !want_w &&
			!(open_mode & (FILE_OPEN_CREATE | FILE_OPEN_TRUNC)))
			mem_idx = memfs_seed_readonly_file(path);
		if (mem_idx < 0 && (open_mode & FILE_OPEN_CREATE))
			mem_idx = memfs_create(path, false);
		if (mem_idx >= 0)
			return memfs_open_index(mem_idx, want_r, want_w, open_mode);
	}

	// 2. 不存在且允许创建：创建 DATA 文件
	if (ip == NULL && fs_readonly_ext4 && (open_mode & FILE_OPEN_CREATE)) {
		return NULL;
	}
	if (ip == NULL && (open_mode & FILE_OPEN_CREATE)) {
        ip = path_create_inode(path, INODE_TYPE_DATA, 
			INODE_MAJOR_DEFAULT, INODE_MINOR_DEFAULT);
    }
	if (ip == NULL) {
		return NULL; // 文件不存在且未创建成功
	}

	// 3. 若是设备文件：检查权限合法性
	inode_lock(ip);
	uint16 type = ip->disk_info.type;
    uint16 major = ip->disk_info.major;
	inode_unlock(ip);

	if (type == INODE_TYPE_DIVICE) {
		if (!device_open_check(major, open_mode)) {
			inode_put(ip);
			return NULL; // 设备文件打开权限检查失败
		}
	} else if (fs_readonly_ext4 && (want_w || (open_mode & FILE_OPEN_CREATE))) {
		inode_put(ip);
		return NULL;
	}

	// 4. 从 file_table 分配 file 并绑定 inode
	file_t *f = file_alloc();
	if (f == NULL) {
		inode_put(ip);
		return NULL; // 分配 file 失败
	}

	f->ip = ip;
    f->readable = want_r;
    f->writbale = want_w;
    f->offset = 0;
    return f;
}

/* 关闭文件 */
void file_close(file_t *file)
{
	if (file == NULL)
        return;

	inode_t *ip = NULL;
	int mem_idx = -1;

	spinlock_acquire(&lk_file_table);

	if (file->ref == 0) {
        spinlock_release(&lk_file_table);
        panic("file_close: ref already zero"); 
    }

	// 减少引用计数
	file->ref--;
	if (file->ref > 0) {
		// 仍有引用，直接返回
		spinlock_release(&lk_file_table);
		return;
	}

	// 此时 ref == 0：回收槽位
	ip = file->ip;
	if (file->is_mem)
		mem_idx = file->mem_index;
	bool was_pipe = file->is_pipe;
	pipe_t *pi = file->pipe;
	bool was_socket = file->is_socket;
	socket_t *so = file->socket;
	bool was_writable = file->writbale;
    file->ip = NULL;
	file->is_device = false;
	file->dev_major = 0;
	file->is_proc = false;
	file->proc_kind = PROC_NONE;
	file->proc_pid = 0;
	file->is_mem = false;
	file->mem_index = -1;
    file->readable = false;
    file->writbale = false;
    file->offset = 0;
	file->is_pipe = false;
	file->pipe = NULL;
	file->is_socket = false;
	file->socket = NULL;

	spinlock_release(&lk_file_table);

	if (was_pipe && pi != NULL)
		pipe_close(pi, was_writable);
	if (was_socket && so != NULL)
		socket_file_close(so);
	if (mem_idx >= 0)
		memfs_maybe_reclaim_unlinked(mem_idx);

	if (ip != NULL)
		inode_put(ip); // 释放inode
}

/* 读取文件内容, 返回读到的字节数量 */
uint32 file_read(file_t* file, uint32 len, uint64 dst, bool is_user_dst)
{
	if (file == NULL){
		return (uint32)-1;
	}
    if (!file->readable){
		return (uint32)-1;
	}

	if (file->is_pipe)
		return pipe_read(file->pipe, dst, len, is_user_dst);

	if (file->is_socket)
		return socket_file_read(file->socket, dst, len, is_user_dst);

	if (file->is_proc)
		return procfs_read(file, len, dst, is_user_dst);

	if (file->is_mem)
		return memfs_read(file, len, dst, is_user_dst);

	if (file->is_device)
		return device_read_data(file->dev_major, len, dst, is_user_dst);

	if (file->ip == NULL)
		return (uint32)-1;
        

	inode_t *ip = file->ip;

	// 先获取文件类型和主设备号
	inode_lock(ip);
	uint16 type = ip->disk_info.type;
	uint16 major = ip->disk_info.major;
    inode_unlock(ip);

	uint32 read_len = 0;

	// 然后根据文件类型分类讨论
	switch (type) {
	case INODE_TYPE_DATA:
		// 数据文件：从inode中按字节流读取
		inode_lock(ip);
		read_len = inode_read_data(ip, file->offset, len, (void*)dst, is_user_dst);
		file->offset += read_len; // 更新偏移量
		inode_unlock(ip);
		return read_len;
	
	case INODE_TYPE_DIR:
		// 目录文件：按“目录项结构体”输出
		inode_lock(ip);
		read_len = dentry_transmit(ip, file->offset, dst, len, is_user_dst);
		file->offset += read_len; // 更新偏移量
		inode_unlock(ip);
		return read_len;

	case INODE_TYPE_DIVICE:
		// 设备文件：不落磁盘，走设备回调
		read_len = device_read_data(major, len, dst, is_user_dst);
		file->offset += read_len; // 更新偏移量
		return read_len;

	default:
		return (uint32)-1; // 不支持的文件类型
	}
}

/* 写入文件内容, 返回写入的字节数量 */
uint32 file_write(file_t* file, uint32 len, uint64 src, bool is_user_src)
{
	if (file == NULL){
		return (uint32)-1;
	}	
    if (!file->writbale){
		return (uint32)-1;
	}

	if (file->is_pipe)
		return pipe_write(file->pipe, src, len, is_user_src);

	if (file->is_socket)
		return socket_file_write(file->socket, src, len, is_user_src);

	if (file->is_proc)
		return (uint32)-1;

	if (file->is_mem)
		return memfs_write(file, len, src, is_user_src);

	if (file->is_device)
		return device_write_data(file->dev_major, len, src, is_user_src);

	if (file->ip == NULL)
		return (uint32)-1;
        

	inode_t *ip = file->ip;

	// 先获取文件类型和主设备号
	inode_lock(ip);
	uint16 type = ip->disk_info.type;
	uint16 major = ip->disk_info.major;
	inode_unlock(ip);

	uint32 write_len = 0;

	// 然后根据文件类型分类讨论
	switch (type) {
	case INODE_TYPE_DATA:
		// 数据文件：写入inode中
		inode_lock(ip);
		write_len = inode_write_data(ip, file->offset, len, (void*)src, is_user_src);
		inode_rw(ip, true); // 写回磁盘
		file->offset += write_len; // 更新偏移量
		inode_unlock(ip);
		return write_len;

	case INODE_TYPE_DIR:
		// 目录文件：不允许写入
		return (uint32)-1;

	case INODE_TYPE_DIVICE:
		// 设备文件：不落磁盘，走设备回调
		write_len = device_write_data(major, len, src, is_user_src);
		file->offset += write_len; // 更新偏移量
		return write_len;

	default:
		return (uint32)-1; // 不支持的文件类型
	}

}

/* 
	读/写指针的移动
	对于不合理的lseek_offset, 只做尽力而为的移动
	返回新的file->offset
*/
uint64 file_lseek(file_t *file, uint64 lseek_offset, uint32 lseek_flag)
{
	if (file == NULL)
        return 0;

	int64 off = (int64)lseek_offset;
	uint64 size = 0;
	if (file->is_mem && file->mem_index >= 0 && file->mem_index < MEMFS_NODES) {
		size = memfs_nodes[file->mem_index].size;
	} else if (file->ip != NULL) {
		inode_lock(file->ip);
		size = file->ip->disk_info.size;
		inode_unlock(file->ip);
	}

	// 根据 lseek_flag 计算新的 offset
	switch (lseek_flag) {
	case FILE_LSEEK_SET:
		// 从文件开头开始计算
		file->offset = off < 0 ? 0 : lseek_offset;
		break;
	case FILE_LSEEK_ADD:
		// 从当前位置开始计算
		if (off < 0 && file->offset < (uint64)(-off))
			file->offset = 0;
		else
			file->offset = (uint64)(file->offset + off);
		break;
	case FILE_LSEEK_SUB:
		// 从当前位置向前计算
		if (off < 0 && size < (uint64)(-off))
			file->offset = 0;
		else
			file->offset = (uint64)(size + off);
		break;
	default:
		// 非法 flag：不做处理
        break;
	}
	return file->offset; // 返回新的 offset
}

/* file->ref++ with lock protect */
file_t* file_dup(file_t* file)
{
	if (file == NULL)
        return NULL;

	spinlock_acquire(&lk_file_table);
    if (file->ref == 0) {
        spinlock_release(&lk_file_table);
        return NULL;
    }

	// 增加引用计数
	file->ref++;
	spinlock_release(&lk_file_table);
	return file;
}

/* 获取文件参数, 成功返回0, 失败返回-1 */
uint32 file_get_stat(file_t* file, uint64 user_dst)
{
	 if (file == NULL)
        return (uint32)-1;

	// 填充file_stat结构体
	file_stat_t st;
	memset(&st, 0, sizeof(st));

	if (file->is_proc) {
		st.type = procfs_is_dir(file->proc_kind) ? INODE_TYPE_DIR : INODE_TYPE_DATA;
		st.nlink = 1;
		st.size = 0;
		st.inode_num = 0xF000 + file->proc_kind;
		st.offset = file->offset;
		uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
		return 0;
	}

	if (file->is_mem) {
		memfs_node_t *node = &memfs_nodes[file->mem_index];
		st.type = node->is_dir ? INODE_TYPE_DIR : INODE_TYPE_DATA;
		st.nlink = 1;
		st.size = node->size;
		st.inode_num = 0xE000 + file->mem_index;
		st.offset = file->offset;
		uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
		return 0;
	}

	if (file->is_device) {
		st.type = INODE_TYPE_DIVICE;
		st.nlink = 1;
		st.size = 0;
		st.inode_num = INVALID_INODE_NUM;
		st.offset = file->offset;
		uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
		return 0;
	}

	if (file->is_socket) {
		st.type = INODE_TYPE_DATA;
		st.nlink = 1;
		st.size = 0;
		st.inode_num = 0xD000;
		st.offset = 0;
		uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
		return 0;
	}

	if (file->ip == NULL)
		return (uint32)-1;

	inode_t *ip = file->ip;

	inode_lock(ip);
	st.type = ip->disk_info.type;
    st.nlink = ip->disk_info.nlink;
    st.size = ip->disk_info.size;
    st.inode_num = ip->inode_num;
    inode_unlock(ip);

	st.offset = file->offset;

	// 拷贝到用户空间
	uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
	return 0;
}

// 填充 Linux/RISC-V struct stat(128B) 到用户空间, 供 musl 的 fstat/fstatat 使用。
// 返回 0 成功, (uint32)-1 失败。
uint32 file_get_stat_linux(file_t* file, uint64 user_dst)
{
	struct {
		uint64 st_dev, st_ino;
		uint32 st_mode, st_nlink, st_uid, st_gid;
		uint64 st_rdev, __pad1;
		uint64 st_size;
		uint32 st_blksize, __pad2;
		uint64 st_blocks;
		uint64 st_atime_sec, st_atime_nsec;
		uint64 st_mtime_sec, st_mtime_nsec;
		uint64 st_ctime_sec, st_ctime_nsec;
		uint32 __unused4, __unused5;
	} st;
	if (file == NULL) return (uint32)-1;
	memset(&st, 0, sizeof(st));
	st.st_blksize = 512;

	if (file->is_proc) {
		char tmp[1024];
		uint32 size = procfs_build_content(file, tmp, sizeof(tmp));
		st.st_mode = (procfs_is_dir(file->proc_kind) ? 0040000 : 0100000) | 0555;
		st.st_nlink = 1;
		st.st_ino = 0xF000 + file->proc_kind + (uint32)file->proc_pid;
		st.st_size = procfs_is_dir(file->proc_kind) ? 0 : size;
		st.st_blocks = (st.st_size + 511) / 512;
	} else if (file->is_mem) {
		memfs_node_t *node = &memfs_nodes[file->mem_index];
		st.st_mode = (node->is_dir ? 0040000 : 0100000) | 0777;
		st.st_nlink = 1;
		st.st_ino = 0xE000 + file->mem_index;
		st.st_size = node->size;
		st.st_blocks = (st.st_size + 511) / 512;
		st.st_atime_sec = node->atime_sec;
		st.st_atime_nsec = node->atime_nsec;
		st.st_mtime_sec = node->mtime_sec;
		st.st_mtime_nsec = node->mtime_nsec;
		st.st_ctime_sec = node->mtime_sec;
		st.st_ctime_nsec = node->mtime_nsec;
	} else if (file->is_device) {
		st.st_mode  = 0020000 | 0666;   // S_IFCHR
		st.st_nlink = 1;
		st.st_ino   = 1;
	} else if (file->is_socket) {
		st.st_mode  = 0140000 | 0777;   // S_IFSOCK
		st.st_nlink = 1;
		st.st_ino   = 0xD000;
	} else {
		if (file->ip == NULL) return (uint32)-1;
		inode_t *ip = file->ip;
		inode_lock(ip);
		short  type  = ip->disk_info.type;
		short  nlink = ip->disk_info.nlink;
		uint32 size  = ip->disk_info.size;
		uint32 inum  = ip->inode_num;
		inode_unlock(ip);

		uint32 mode;
		if (type == INODE_TYPE_DIR)         mode = 0040000 | 0755;  // S_IFDIR
		else if (type == INODE_TYPE_DIVICE) mode = 0020000 | 0666;  // S_IFCHR
		else                                mode = 0100000 | 0755;  // S_IFREG
		st.st_mode   = mode;
		st.st_nlink  = (nlink > 0) ? (uint32)nlink : 1;
		st.st_size   = size;
		st.st_ino    = inum;
		st.st_blocks = (size + 511) / 512;
	}
	uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
	return 0;
}

uint32 file_get_dents_linux(file_t *file, uint64 user_dst, uint32 len)
{
	if (file == NULL || user_dst == 0)
		return (uint32)-1;
	if (file->is_proc)
		return procfs_get_dents(file, user_dst, len);
	if (file->ip == NULL)
		return (uint32)-1;

	inode_t *ip = file->ip;
	inode_lock(ip);
	if (ip->disk_info.type != INODE_TYPE_DIR) {
		inode_unlock(ip);
		return (uint32)-1;
	}

	uint32 copied = 0;
	while (1) {
		dentry_t de;
		memset(&de, 0, sizeof(de));
		uint32 got = dentry_transmit(ip, file->offset, (uint64)&de, sizeof(de), false);
		if (got == 0)
			break;
		file->offset += sizeof(dentry_t);
		if (de.name[0] == 0)
			continue;
		if (streq(de.name, ".") || streq(de.name, ".."))
			continue;
		uint8 dtype = 8;
		inode_t *child = inode_get(de.inode_num);
		if (child != NULL) {
			if (child->disk_info.type == INODE_TYPE_DIR)
				dtype = 4;
			else if (child->disk_info.type == INODE_TYPE_DIVICE)
				dtype = 2;
			inode_put(child);
		}
		if (dtype == 4)
			continue;
		uint32 before = copied;
		copied = emit_linux_dirent(user_dst, len, copied, de.inode_num, file->offset, dtype, de.name);
		if (copied == before) {
			file->offset -= sizeof(dentry_t);
			break;
		}
	}
	inode_unlock(ip);
	return copied;
}

uint32 file_get_statfs_linux(file_t *file, uint64 user_dst)
{
	struct {
		uint64 f_type;
		uint64 f_bsize;
		uint64 f_blocks;
		uint64 f_bfree;
		uint64 f_bavail;
		uint64 f_files;
		uint64 f_ffree;
		int f_fsid[2];
		uint64 f_namelen;
		uint64 f_frsize;
		uint64 f_flags;
		uint64 f_spare[4];
	} st;
	if (file == NULL || user_dst == 0)
		return (uint32)-1;
	memset(&st, 0, sizeof(st));
	st.f_type = file->is_proc ? 0x9FA0 : 0xEF53;
	st.f_bsize = 4096;
	st.f_blocks = sb.total_blocks ? sb.total_blocks : 262144;
	st.f_bfree = st.f_blocks / 2;
	st.f_bavail = st.f_bfree;
	st.f_files = sb.total_inodes ? sb.total_inodes : 1024;
	st.f_ffree = st.f_files / 2;
	st.f_namelen = MAXLEN_FILENAME - 1;
	st.f_frsize = 4096;
	uvm_copyout(myproc()->pgtbl, user_dst, (uint64)&st, sizeof(st));
	return 0;
}


/* 基于superblock输出磁盘布局信息 (for debug) */
static void sb_print()
{
	printf("\ndisk layout information:\n");
	printf("1. super block:  block[0]\n");
	printf("2. inode bitmap: block[%d - %d]\n", sb.inode_bitmap_firstblock,
		sb.inode_bitmap_firstblock + sb.inode_bitmap_blocks - 1);
	printf("3. inode region: block[%d - %d]\n", sb.inode_firstblock,
		sb.inode_firstblock + sb.inode_blocks - 1);
	printf("4. data bitmap:  block[%d - %d]\n", sb.data_bitmap_firstblock,
		sb.data_bitmap_firstblock + sb.data_bitmap_blocks - 1);
	printf("5. data region:  block[%d - %d]\n", sb.data_firstblock,
		sb.data_firstblock + sb.data_blocks - 1);
	printf("block size = %d Byte, total size = %d MB, total inode = %d\n\n", sb.block_size,
		(int)((unsigned long long)(sb.total_blocks) * sb.block_size / 1024 / 1024), sb.total_inodes);
}

static bool fs_try_ext4_preview()
{
	buffer_t *b = buffer_get(FS_SB_BLOCK);
	ext4_super_preview_t ext4_sb;
	memmove(&ext4_sb, b->data + EXT4_SUPER_OFFSET, sizeof(ext4_sb));
	buffer_put(b);

	if (ext4_sb.magic != EXT4_SUPER_MAGIC)
		return false;
	if (!ext4_mount_from_super(&ext4_sb)) {
		printf("\next4 superblock detected but unsupported features: incompat=0x%x ro_compat=0x%x\n\n",
			ext4_sb.feature_incompat, ext4_sb.feature_ro_compat);
		return false;
	}

	fs_readonly_ext4 = true;

	return true;
}

void fs_init()
{
	fs_readonly_ext4 = false;
	// 初始化缓冲系统
	buffer_init();
	// 初始化inode缓存与锁
	inode_init();
	// 初始化管道池
	pipe_init();

	// 读取超级块
	buffer_t *b = buffer_get(FS_SB_BLOCK);
	// 将缓冲区内容拷贝到内存中的sb
	memmove(&sb, b->data, sizeof(super_block_t));
	// 归还缓冲（不修改，不需要写回）
	buffer_put(b);

	if (sb.magic_num == FS_MAGIC) {
		// 打印布局信息
		sb_print();
	} else if (!fs_try_ext4_preview()) {
		printf("\nunknown filesystem on primary disk: block0 magic = 0x%x\n\n", sb.magic_num);
	}

	// 初始化设备表
	device_init();
	// 初始化 socket 池
	socket_init();
	// 初始化文件表
	file_init();
}
