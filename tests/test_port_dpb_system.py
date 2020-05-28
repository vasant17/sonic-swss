from swsscommon import swsscommon
import redis
import time
import os
import pytest
from pytest import *
import json
import re
from port_dpb import Port
from port_dpb import DPB

@pytest.mark.usefixtures('dpb_setup_fixture')
@pytest.mark.usefixtures('dvs_acl_manager')
class TestPortDPBSystem(object):

    def verify_only_ports_exist(self, dvs, port_names):
        all_port_names = ["Ethernet0", "Ethernet1", "Ethernet2", "Ethernet3"]
        for port_name in all_port_names:
            p = Port(dvs, port_name)
            if port_name in port_names:
                assert(p.exists_in_config_db() == True)
                assert(p.exists_in_app_db() == True)
                assert(p.exists_in_asic_db() == True)
            else:
                assert(p.exists_in_config_db() == False)
                assert(p.exists_in_app_db() == False)
                assert(p.exists_in_asic_db() == False)

    '''
    |-----------------------------------------------------------------------------------------------------
    |                   | 1X40G | 1X100G | 4X10G | 4X25G | 2X50G | 2x25G(2)+1x50G(2) | 1x50G(2)+2x25G(2) |
    |-----------------------------------------------------------------------------------------------------
    | 1X40G             |  NA   |        |       |       |       |                   |                   |
    |-----------------------------------------------------------------------------------------------------
    | 1X100G            |       |   NA   |       |  P    |  P    |        P          |        P          |
    |-----------------------------------------------------------------------------------------------------
    | 4X10G             |       |        |  NA   |       |       |                   |                   |
    |-----------------------------------------------------------------------------------------------------
    | 4X25G             |       |   P    |       |  NA   |  P    |        P          |        P          |
    |-----------------------------------------------------------------------------------------------------
    | 2X50G             |       |   P    |       |  P    |  NA   |        P          |        P          |
    |-----------------------------------------------------------------------------------------------------
    | 2x25G(2)+1x50G(2) |       |   P    |       |  P    |  P    |        NA         |        P          |
    |-----------------------------------------------------------------------------------------------------
    | 1x50G(2)+2x25G(2) |       |   P    |       |  P    |  P    |        P          |        NA         |
    |-----------------------------------------------------------------------------------------------------

    NA    --> Not Applicable
    P     --> Pass
    F     --> Fail
    Empty --> Not Tested
    '''

    '''
    @pytest.mark.skip()
    '''
    def test_port_breakout_one(self, dvs):
        dvs.setup_db()
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0"])

        ########## 2X50G to all other modes and vice-versa ##########

        dvs.change_port_breakout_mode("Ethernet0", "2x50G")
        dvs.verify_port_breakout_mode("Ethernet0", "2x50G")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2"])
        print "**** 1X100G --> 2x50G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "4x25G[10G]")
        dvs.verify_port_breakout_mode("Ethernet0", "4x25G[10G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2", "Ethernet3"])
        print "**** 2x50G --> 4X25G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "2x50G")
        dvs.verify_port_breakout_mode("Ethernet0", "2x50G")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2"])
        print "**** 4X25G --> 2x50G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2"])
        print "**** 2X50G --> 2x25G(2)+1x50G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "2x50G")
        dvs.verify_port_breakout_mode("Ethernet0", "2x50G")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2"])
        print "**** 2x25G(2)+1x50G(2) --> 2x50G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2", "Ethernet3"])
        print "**** 2X50G --> 1x50G(2)+2x25G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "2x50G")
        dvs.verify_port_breakout_mode("Ethernet0", "2x50G")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2"])
        print "**** 1x50G(2)+2x25G(2) --> 2x50G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "1x100G[40G]")
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0"])
        print "**** 2x50G --> 1x100G passed ****"

        ########## 4X25G to all other modes and vice-versa ##########

        dvs.change_port_breakout_mode("Ethernet0", "4x25G[10G]")
        dvs.verify_port_breakout_mode("Ethernet0", "4x25G[10G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2", "Ethernet3"])
        print "**** 1x100G --> 4X25G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2", "Ethernet3"])
        print "**** 4X25G --> 1x50G(2)+2x25G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "4x25G[10G]")
        dvs.verify_port_breakout_mode("Ethernet0", "4x25G[10G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2", "Ethernet3"])
        print "**** 1x50G(2)+2x25G(2) --> 4X25G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2"])
        print "**** 4X25G --> 2x25G(2)+1x50G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "4x25G[10G]")
        dvs.verify_port_breakout_mode("Ethernet0", "4x25G[10G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2", "Ethernet3"])
        print "**** 2x25G(2)+1x50G(2) --> 4X25G passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "1x100G[40G]")
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0"])
        print "**** 4x25G --> 1x100G passed ****"

        ########## 1x50G(2)+2x25G(2) to all other modes and vice-versa ##########

        dvs.change_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2", "Ethernet3"])
        print "**** 1X100G --> 1x50G(2)+2x25G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2"])
        print "**** 1x50G(2)+2x25G(2) --> 2x25G(2)+1x50G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "1x50G(2)+2x25G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet2", "Ethernet3"])
        print "**** 2x25G(2)+1x50G(2) --> 1x50G(2)+2x25G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "1x100G[40G]")
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0"])
        print "**** 1x50G(2)+2x25G(2) --> 1x100G passed ****"

        ########## 2x25G(2)+1x50G(2) to all other modes and vice-versa ##########

        dvs.change_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        dvs.verify_port_breakout_mode("Ethernet0", "2x25G(2)+1x50G(2)")
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2"])
        print "**** 1x100G --> 2x25G(2)+1x50G(2) passed ****"

        dvs.change_port_breakout_mode("Ethernet0", "1x100G[40G]")
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")
        self.verify_only_ports_exist(dvs, ["Ethernet0"])
        print "**** 2x25G(2)+1x50G(2) --> 1x100G passed ****"

    @pytest.mark.skip()
    def test_port_breakout_with_acl(self, dvs):
        dvs.setup_db()
        dpb = DPB()

        # Create ACL table "test" and bind it to Ethernet0
        bind_ports = ["Ethernet0"]
        self.dvs_acl.create_acl_table("test", "L3", bind_ports)

        # Verify ACL teable is created
        self.dvs_acl.verify_acl_table_count(1)

        # Verify that ACL group OID is created.
        # Just FYI: Usually one ACL group OID is created per port,
        #           even when port is bound to multiple ACL tables
        self.dvs_acl.verify_acl_group_num(1)

        # Verify that port is correctly bound to table by looking into
        # ACL member table, which binds ACL group OID of a port and
        # ACL table OID.
        acl_table_ids = self.dvs_acl.get_acl_table_ids()
        self.dvs_acl.verify_acl_table_ports_binding(bind_ports, acl_table_ids[0])

        # Verify current breakout mode, perform breakout without force dependency
        # delete option
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")
        dvs.change_port_breakout_mode("Ethernet0", "4x25G[10G]")

        # Verify that breakout did NOT succeed
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")

        # Do breakout with force option, and verify that it succeeds
        dvs.change_port_breakout_mode("Ethernet0", "4x25G[10G]", "-f")
        dpb.verify_port_breakout_mode(dvs, "Ethernet0", "4x25G[10G]")

        # Verify port is removed from ACL table
        self.dvs_acl.verify_acl_table_count(1)

        #TBD: Uncomment this, or explain why Ethernet0 is being added back to ACL table
        # Also, string "None" is being added as port to ACL port list after the breakout
        #self.dvs_acl.verify_acl_group_num(0)

        # Verify child ports are created.
        self.verify_only_ports_exist(dvs, ["Ethernet0", "Ethernet1", "Ethernet2", "Ethernet3"])

        # Enable below snippet after fixing the above issues
        '''
        # Move back to 1x100G[40G] mode and verify current mode
        dvs.change_port_breakout_mode("Ethernet0", "1x100G[40G]", "-f")
        dpb.verify_port_breakout_mode(dvs, "Ethernet0", "1x100G[40G]")
        '''

        # Remove ACL table and verify the same
        self.dvs_acl.remove_acl_table("test")
        self.dvs_acl.verify_acl_table_count(0)

    def test_dpb_arp_flush(self, dvs):
        dvs.setup_db()
        self.setup_db(dvs);

        self.clear_srv_config(dvs)

        # Create l3 interface
        rif_oid = self.create_l3_intf("Ethernet0", "")

        # set ip address
        self.add_ip_address("Ethernet0", "10.0.0.0/31")

        # bring up interface
        self.set_admin_status("Ethernet0", "up")

        # Set IP address and default route
        dvs.servers[0].runcmd("ip address add 10.0.0.1/31 dev eth0")
        dvs.servers[0].runcmd("ip route add default via 10.0.0.0")

        # Get neighbor and ARP entry
        dvs.servers[0].runcmd("ping -c 1 10.0.0.0")

        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_NEIGHBOR_ENTRY")
        intf_entries = tbl.getKeys()
        assert len(intf_entries) == 1
        route = json.loads(intf_entries[0])
        assert route["ip"] == "10.0.0.1"
        assert route["rif"] == rif_oid
        (status, fvs) = tbl.get(intf_entries[0])
        assert status == True

        fvs_dict = dict(fvs)
        assert fvs_dict["SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS"] == "46:C1:84:2C:3B:4A"

        # Breakout port and make sure NEIGHBOR entry is removed
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")
        dvs.change_port_breakout_mode("Ethernet0", "4x25G[10G]", "-f")
        dvs.verify_port_breakout_mode("Ethernet0", "4x25G[10G]")

        #Verify ARP/Neighbor entry is removed
        intf_entries = tbl.getKeys()
        assert len(intf_entries) == 0

        dvs.change_port_breakout_mode("Ethernet0", "1x100G[40G]")
        dvs.verify_port_breakout_mode("Ethernet0", "1x100G[40G]")

    """
    Below utility functions are required by test_dpb_arp_flush
    TBD: Introduce dvs_neigbor.py function and move these methods to
         that file. Change the code in test_dpb_arp_flush accordingly.
    """
    def setup_db(self, dvs):
        self.pdb = swsscommon.DBConnector(0, dvs.redis_sock, 0)
        self.adb = swsscommon.DBConnector(1, dvs.redis_sock, 0)
        self.cdb = swsscommon.DBConnector(4, dvs.redis_sock, 0)

    def set_admin_status(self, interface, status):
        tbl = swsscommon.Table(self.cdb, "PORT")
        fvs = swsscommon.FieldValuePairs([("admin_status", status)])
        tbl.set(interface, fvs)
        time.sleep(1)

    def create_l3_intf(self, interface, vrf_name):
        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_ROUTER_INTERFACE")
        initial_entries = set(tbl.getKeys())

        tbl = swsscommon.Table(self.cdb, "INTERFACE")
        if len(vrf_name) == 0:
            fvs = swsscommon.FieldValuePairs([("NULL", "NULL")])
        else:
            fvs = swsscommon.FieldValuePairs([("vrf_name", vrf_name)])
        tbl.set(interface, fvs)
        time.sleep(1)

        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_ROUTER_INTERFACE")
        current_entries = set(tbl.getKeys())
        assert len(current_entries - initial_entries) == 1
        return list(current_entries - initial_entries)[0]


    def add_ip_address(self, interface, ip):
        tbl = swsscommon.Table(self.cdb, "INTERFACE")
        fvs = swsscommon.FieldValuePairs([("NULL", "NULL")])
        tbl.set(interface + "|" + ip, fvs)
        time.sleep(1)

    def clear_srv_config(self, dvs):
        dvs.servers[0].runcmd("ip address flush dev eth0")
        dvs.servers[1].runcmd("ip address flush dev eth0")
        dvs.servers[2].runcmd("ip address flush dev eth0")
        dvs.servers[3].runcmd("ip address flush dev eth0") 
    
