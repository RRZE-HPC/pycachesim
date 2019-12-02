from cachesim import CacheSimulator, Cache, MainMemory
import fileinput
from kerncraft import machinemodel as mm

# mem = MainMemory()
# l3 = Cache("L3", 8192, 16, 64, "LRU")  # 20MB: 20480 sets, 16-ways with cacheline size of 64 bytes
# mem.load_to(l3)
# mem.store_from(l3)
# l2 = Cache("L2", 256, 4, 64, "LRU", store_to=l3, load_from=l3)  # 256KB
# l1 = Cache("L1", 32, 8, 64, "LRU", store_to=l2, load_from=l2)  # 32KB
# cs = CacheSimulator(l1, mem)

machine = mm.MachineModel("./HaswellEP_E5-2695v3_CoD.yml")
cs = machine.get_cachesim()

counter = 0
loadcounter = 0
loadcounter2 = 0
storecounter = 0
for line in fileinput.input():
    line = line.rstrip()
    if (line):
        try:
            sLine = line.split(" ")
            if (sLine[2] == "1"):
                cs.load(int(sLine[0]), int(sLine[1]))
                loadcounter = loadcounter + 1
            elif (sLine[2] == "2"):
                cs.load(int(sLine[0]), int(sLine[1]))
                loadcounter2 = loadcounter + 1
            else:
                cs.store(int(sLine[0]), int(sLine[1]))
                storecounter = storecounter + 1
        except:
            print ("Line: " + line)
    counter = counter + 1

cs.force_write_back()
cs.print_stats()
print ("\nInstructions read: " + str(counter) + "\nLoads: " + str(loadcounter) + "\nLoads2: " + str(loadcounter2) + "\nStores: " + str(storecounter))