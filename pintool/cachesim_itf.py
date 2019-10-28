from cachesim import CacheSimulator, Cache, MainMemory

def init_cachesim():
    mem = MainMemory()
    l3 = Cache("L3", 20480, 16, 64, "LRU")  # 20MB: 20480 sets, 16-ways with cacheline size of 64 bytes
    mem.load_to(l3)
    mem.store_from(l3)
    l2 = Cache("L2", 512, 8, 64, "LRU", store_to=l3, load_from=l3)  # 256KB
    l1 = Cache("L1", 64, 8, 64, "LRU", store_to=l2, load_from=l2)  # 32KB
    return CacheSimulator(l1, mem)

def load(cs, addr, size=1):
    cs.load(addr, length=size)

def store(cs, addr, size=1):
    cs.store(cs, addr, length=size)  # Loads from address 512 until (exclusive) 520 (eight bytes)

def finlize(cs):
    cs.force_write_back()
    cs.print_stats()