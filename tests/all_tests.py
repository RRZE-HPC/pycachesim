#!/usr/bin/env python
import unittest
import sys

suite = unittest.TestLoader().loadTestsFromNames(
    [
        'test',
    ]
)

testresult = unittest.TextTestRunner(verbosity=1).run(suite)
sys.exit(0 if testresult.wasSuccessful() else 1)
