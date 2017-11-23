from __future__ import print_function
import unittest
import socket
import cvmfs_globals
cvmfs_globals.CVMFS_UNITTESTS = True

import cvmfs_geo
from cvmfs_geo import distance_on_unit_sphere
from cvmfs_geo import addr_geoinfo
from cvmfs_geo import name_geoinfo
from cvmfs_geo import geosort_servers

###
# Simulate a small geo IP database, since we can't always
#  expect a full one to be available.  IPv4 addresses are
#  always preferred, so for those with IPv6 use only IPv6.

def getaddrs(name, type):
    addrs = []
    for info in socket.getaddrinfo(name,80,0,0,socket.IPPROTO_TCP):
        if info[0] == type:
            addrs.append(info[4][0])
    return addrs

CERNgeo = {
    'latitude': 46.2324,
    'longitude': 6.0502
}
CERNname = 'cvmfs-stratum-one.cern.ch'
CERNaddrs = getaddrs(CERNname, socket.AF_INET6)
FNALgeo = {
    'latitude': 41.7768,
    'longitude': -88.4604
}
FNALname = 'cvmfs.fnal.gov'
FNALaddrs = getaddrs(FNALname, socket.AF_INET)
IHEPgeo = {
    'latitude': 39.9289,
    'longitude': 116.3883
}
IHEPname = 'cvmfs-stratum-one.ihep.ac.cn'
IHEPaddrs = getaddrs(IHEPname, socket.AF_INET)
RALgeo = {
    'latitude': 51.75,
    'longitude': -1.25
}
RALname = 'cernvmfs.gridpp.rl.ac.uk'
RALaddrs = getaddrs(RALname, socket.AF_INET6)

class giIPv4TestDb():
    def record_by_addr(self, addr):
        if addr in FNALaddrs:
            return FNALgeo
        if addr in IHEPaddrs:
            return IHEPgeo
        return None

class giIPv6TestDb():
    def record_by_addr_v6(self, addr):
        if addr in CERNaddrs:
            return CERNgeo
        if addr in RALaddrs:
            return RALgeo
        return None

cvmfs_geo.gi = giIPv4TestDb()
cvmfs_geo.gi6 = giIPv6TestDb()

####

class GeoTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def test1Distance(self):
        self.assertEqual(0.0, distance_on_unit_sphere(0, 0, 0, 0))
        self.assertAlmostEqual(1.11458455,
            distance_on_unit_sphere(FNALgeo['latitude'], FNALgeo['longitude'],
                                    CERNgeo['latitude'], CERNgeo['longitude']))
        self.assertAlmostEqual(1.11458455,
            distance_on_unit_sphere(CERNgeo['latitude'], CERNgeo['longitude'],
                                    FNALgeo['latitude'], FNALgeo['longitude']))
        self.assertAlmostEqual(1.6622382,
            distance_on_unit_sphere(IHEPgeo['latitude'], IHEPgeo['longitude'],
                                    FNALgeo['latitude'], FNALgeo['longitude']))
        self.assertAlmostEqual(0.1274021,
            distance_on_unit_sphere(CERNgeo['latitude'], CERNgeo['longitude'],
                                    RALgeo['latitude'],  RALgeo['longitude']))
        self.assertAlmostEqual(1.2830254,
            distance_on_unit_sphere(IHEPgeo['latitude'], IHEPgeo['longitude'],
                                    RALgeo['latitude'],  RALgeo['longitude']))
        # surprisingly, CERN is slightly further from IHEP than RAL
        self.assertAlmostEqual(1.2878979,
            distance_on_unit_sphere(IHEPgeo['latitude'], IHEPgeo['longitude'],
                                    CERNgeo['latitude'], CERNgeo['longitude']))

    def test2AddrGeoinfo(self):
        self.assertEqual(CERNgeo, addr_geoinfo(CERNaddrs[0]))
        self.assertEqual(FNALgeo, addr_geoinfo(FNALaddrs[0]))
        self.assertEqual(IHEPgeo, addr_geoinfo(IHEPaddrs[0]))
        self.assertEqual(RALgeo,  addr_geoinfo(RALaddrs[0]))

    def test3NameGeoinfo(self):
        self.assertEqual(0, len(cvmfs_geo.geo_cache))
        now = 0
        self.assertEqual(CERNgeo, name_geoinfo(now, CERNname))
        self.assertEqual(FNALgeo, name_geoinfo(now, FNALname))
        self.assertEqual(IHEPgeo, name_geoinfo(now, IHEPname))
        self.assertEqual(RALgeo,  name_geoinfo(now, RALname))
        self.assertEqual(4, len(cvmfs_geo.geo_cache))

        # test the caching, when there's no database available
        savegi = cvmfs_geo.gi
        savegi6 = cvmfs_geo.gi6
        cvmfs_geo.gi = None
        cvmfs_geo.gi6 = None
        now = 1
        self.assertEqual(CERNgeo, name_geoinfo(now, CERNname))
        self.assertEqual(FNALgeo, name_geoinfo(now, FNALname))
        self.assertEqual(IHEPgeo, name_geoinfo(now, IHEPname))
        self.assertEqual(RALgeo,  name_geoinfo(now, RALname))
        cvmfs_geo.gi = savegi
        cvmfs_geo.gi6 = savegi6

    def test4GeosortServers(self):
        self.assertEqual([True, [3, 0, 1, 2]],
            geosort_servers(0, RALgeo, [CERNname, FNALname, IHEPname, RALname]))
        self.assertEqual([True, [0, 3, 2, 1]],
            geosort_servers(0, RALgeo, [RALname, IHEPname, FNALname, CERNname]))
        self.assertEqual([True, [1, 0, 3, 2]],
            geosort_servers(0, IHEPgeo, [RALname, IHEPname, FNALname, CERNname]))
        self.assertEqual([True, [2, 3, 0, 1]],
            geosort_servers(0, CERNgeo, [FNALname, IHEPname, CERNname, RALname]))
        self.assertEqual([True, [3, 2, 1, 0]],
            geosort_servers(0, FNALgeo, [IHEPname, CERNname, RALname, FNALname]))


if __name__ == '__main__':
    unittest.main()
