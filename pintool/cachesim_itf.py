from cachesim import CacheSimulator, Cache, MainMemory
import fileinput

mem = MainMemory()
l3 = Cache("L3", 20480, 16, 64, "LRU")  # 20MB: 20480 sets, 16-ways with cacheline size of 64 bytes
mem.load_to(l3)
mem.store_from(l3)
l2 = Cache("L2", 512, 8, 64, "LRU", store_to=l3, load_from=l3)  # 256KB
l1 = Cache("L1", 64, 8, 64, "LRU", store_to=l2, load_from=l2)  # 32KB
cs = CacheSimulator(l1, mem)

for line in fileinput.input():
    line.rstrip()
    sLine = line.split(" ")
    if (bool(sLine[2])):
        cs.load(int(sLine[0]), int(sLine[1]))
    else:
        cs.store(int(sLine[0]), int(sLine[1]))

cs.force_write_back()
cs.print_stats()