"""
Unit tests for cachesim module
"""
from __future__ import print_function

import unittest
import tempfile
import shutil
import textwrap

from cachesim import CacheSimulator, Cache, MainMemory, CacheVisualizer


class TestVisualizer(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.maxDiff = None

    def tearDown(self):
        shutil.rmtree(self.temp_dir)
        self.temp_dir = None

    def test_fill_nocl(self):
        mem = MainMemory()
        l3 = Cache("L3", 4, 8, 1, "LRU")
        mem.load_to(l3)
        mem.store_from(l3)
        l2 = Cache("L2", 4, 4, 1, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 1, "LRU", store_to=l2, load_from=l2)
        cs = CacheSimulator(l1, mem)

        cs.load(range(0, 32))
        cs.load(range(16, 48))

        cv = CacheVisualizer(cs, [6, 8], element_size=1, name="fill_nocl",
                             filename_base=self.temp_dir+"/fill_nocl")
        cv.visualize()
        with open(self.temp_dir+"/fill_nocl_0.vtk") as f:
            self.assertEqual(f.read(), textwrap.dedent("""\
                # vtk DataFile Version 4.0
                CACHESIM VTK output
                ASCII
                DATASET STRUCTURED_POINTS
                DIMENSIONS 2 9 7
                ORIGIN 0 0 0
                SPACING 1 1 1
                CELL_DATA 48
                FIELD DATA 1
                
                Data_arr 3 48 double
                """) + "0 0 0\n"*16 + "0 0 1\n"*16 + "0 1 2\n"*8 + "2 2 2\n"*8)

    def test_fill(self):
        mem = MainMemory()
        l3 = Cache("L3", 4, 8, 8, "LRU")
        mem.load_to(l3)
        mem.store_from(l3)
        l2 = Cache("L2", 4, 4, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 8, "LRU", store_to=l2, load_from=l2)
        cs = CacheSimulator(l1, mem)

        cs.load(range(0, 512))
        cs.load(range(448, 576))

        cv = CacheVisualizer(cs, [10, 10], name="fill",
                             filename_base=self.temp_dir+"/fill")
        cv.visualize()
        with open(self.temp_dir+"/fill_0.vtk") as f:
            self.assertEqual(f.read(), textwrap.dedent("""\
                # vtk DataFile Version 4.0
                CACHESIM VTK output
                ASCII
                DATASET STRUCTURED_POINTS
                DIMENSIONS 2 11 11
                ORIGIN 0 0 0
                SPACING 1 1 1
                CELL_DATA 100
                FIELD DATA 1
                
                Data_arr 3 100 double
                """) + "0 0 0\n"*40 + "0 0 1\n"*16 + "0 1 2\n"*8 + "2 2 2\n"*8 + "0 0 0\n"*28)
        cs.load(range(256, 32 * 24))
        cv.visualize()
        with open(self.temp_dir+"/fill_1.vtk") as f:
            self.assertEqual(f.read(), textwrap.dedent("""\
                # vtk DataFile Version 4.0
                CACHESIM VTK output
                ASCII
                DATASET STRUCTURED_POINTS
                DIMENSIONS 2 11 11
                ORIGIN 0 0 0
                SPACING 1 1 1
                CELL_DATA 100
                FIELD DATA 1
                
                Data_arr 3 100 double
                """) + "0 0 0\n"*64 + "0 0 1\n"*16 + "0 1 2\n"*8 + "2 2 2\n"*8 + "0 0 0\n"*4)


    def _get_SandyEP_caches(self):
        # Cache hierarchy as found in a Sandy Brige EP:
        cacheline_size = 64
        mem = MainMemory()
        l3 = Cache("L3", 20480, 16, cacheline_size,
                   "LRU",
                   write_back=True, write_allocate=True)  # 20MB 16-ways
        mem.load_to(l3)
        mem.store_from(l3)
        l2 = Cache("L2", 512, 8, cacheline_size,
                   "LRU",
                   write_back=True, write_allocate=True,
                   store_to=l3, load_from=l3)  # 256kB 8-ways
        l1 = Cache("L1", 64, 8, cacheline_size,
                   "LRU",
                   write_back=True, write_allocate=True,
                   store_to=l2, load_from=l2)  # 32kB 8-ways
        mh = CacheSimulator(l1, mem)
        return mh, l1, l2, l3, mem, cacheline_size

    def test_large_fill(self):
        cs, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()

        cs.load(range(16 * 1024, 24 * 1024), 8)

        cv_l1l2 = CacheVisualizer(cs, [4, 8, 2], cache=[l1, l2],
                                  start_address=23*1024, name="large_fill",
                                  element_size=64, filename_base=self.temp_dir+"/large_fill_l1l2")
        cv_l1 = CacheVisualizer(cs, [4, 4, 2], cache=l1,
                                name="large_fill", element_size=64, start_address=15*1024,
                                filename_base=self.temp_dir+"/large_fill_l1")
        cv_l1l2.visualize()
        with open(self.temp_dir+"/large_fill_l1l2_0.vtk") as f:
            self.assertEqual(f.read(), textwrap.dedent("""\
            # vtk DataFile Version 4.0
            CACHESIM VTK output
            ASCII
            DATASET STRUCTURED_POINTS
            DIMENSIONS 3 9 5
            ORIGIN 0 0 0
            SPACING 1 1 1
            CELL_DATA 64
            FIELD DATA 1
            
            Data_arr 2 64 double
            """) + "2 2\n"*17 + "0 0\n"*47)

        cv_l1.visualize()
        with open(self.temp_dir+"/large_fill_l1_0.vtk") as f:
            self.assertEqual(f.read(), textwrap.dedent("""\
            # vtk DataFile Version 4.0
            CACHESIM VTK output
            ASCII
            DATASET STRUCTURED_POINTS
            DIMENSIONS 3 5 5
            ORIGIN 0 0 0
            SPACING 1 1 1
            CELL_DATA 32
            FIELD DATA 1
            
            Data_arr 1 32 double
            """) + "0\n"*16 + "2\n"*16)
