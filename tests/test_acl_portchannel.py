from swsscommon import swsscommon

import time

class TestPortChannelAcl(object):
    def setup_db(self, dvs):
        self.pdb = swsscommon.DBConnector(0, dvs.redis_sock, 0)
        self.adb = swsscommon.DBConnector(1, dvs.redis_sock, 0)
        self.cdb = swsscommon.DBConnector(4, dvs.redis_sock, 0)

    def create_port_channel(self, dvs, alias):
        tbl = swsscommon.Table(self.cdb, "PORTCHANNEL")
        fvs = swsscommon.FieldValuePairs([("admin_status", "up"),
                                          ("mtu", "9100")])
        tbl.set(alias, fvs)
        time.sleep(1)

    def remove_port_channel(self, dvs, alias):
        tbl = swsscommon.Table(self.cdb, "PORTCHANNEL")
        tbl._del(alias)
        time.sleep(1)

    def add_port_channel_members(self, dvs, lag, members):
        tbl = swsscommon.Table(self.cdb, "PORTCHANNEL_MEMBER")
        fvs = swsscommon.FieldValuePairs([("NULL", "NULL")])
        for member in members:
            tbl.set(lag + "|" + member, fvs)
            time.sleep(1)

    def activate_port_channel_members(self, dvs, lag, members):
        tbl = swsscommon.ProducerStateTable(self.pdb, "LAG_MEMBER_TABLE")
        fvs = swsscommon.FieldValuePairs([("status", "enabled")])

        for member in members:
            tbl.set(lag + ":" + member, fvs)
            time.sleep(1)

    def remove_port_channel_members(self, dvs, lag, members):
        tbl = swsscommon.Table(self.cdb, "PORTCHANNEL_MEMBER")
        for member in members:
            tbl._del(lag + "|" + member)
            time.sleep(1)

    def create_acl_table(self, dvs, table_name, ports):
        tbl = swsscommon.Table(self.cdb, "ACL_TABLE")
        fvs = swsscommon.FieldValuePairs([("POLICY_DESC", "LAG_ACL_TEST"),
                                          ("TYPE", "L3"),
                                          ("PORTS", ports)])
        tbl.set(table_name, fvs)
        time.sleep(1)

    def remove_acl_table(self, dvs, table_name):
        tbl = swsscommon.Table(self.cdb, "ACL_TABLE")
        tbl._del(table_name)
        time.sleep(1)

    def create_acl_rule(self, dvs, table_name, rule_name):
        tbl = swsscommon.Table(self.cdb, "ACL_RULE")
        fvs = swsscommon.FieldValuePairs([("PRIORITY", "88"),
                                          ("PACKET_ACTION", "FORWARD"),
                                          ("L4_SRC_PORT", "8888")])
        tbl.set(table_name + "|" + rule_name, fvs)
        time.sleep(1)

    def remove_acl_rule(self, dvs, table_name, rule_name):
        tbl = swsscommon.Table(self.cdb, "ACL_RULE")
        tbl._del(table_name + "|" + rule_name, fvs)
        time.sleep(1)

    def check_asic_table_existed(self, dvs):
        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_LAG")
        lag = tbl.getKeys()[0]
        (status, fvs) = tbl.get(lag)
        assert status == True
        assert len(fvs) == 2
        for fv in fvs:
            if fv[0] == "SAI_LAG_ATTR_INGRESS_ACL":
                table_group_id = fv[1]
            elif fv[0] == "NULL":
                continue
            else:
                assert False

        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_ACL_TABLE_GROUP")
        (status, fvs) = tbl.get(table_group_id)
        assert status == True
        assert len(fvs) == 3
        for fv in fvs:
            if fv[0] == "SAI_ACL_TABLE_GROUP_ATTR_ACL_STAGE":
                assert fv[1] == "SAI_ACL_STAGE_INGRESS"
            elif fv[0] == "SAI_ACL_TABLE_GROUP_ATTR_ACL_BIND_POINT_TYPE_LIST":
                assert fv[1] == "1:SAI_ACL_BIND_POINT_TYPE_LAG"
            elif fv[0] == "SAI_ACL_TABLE_GROUP_ATTR_TYPE":
                assert fv[1] == "SAI_ACL_TABLE_GROUP_TYPE_PARALLEL"
            else:
                assert False

        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_ACL_TABLE_GROUP_MEMBER")
        member = tbl.getKeys()[0]
        (status, fvs) = tbl.get(member)
        assert status == True
        assert len(fvs) == 3
        for fv in fvs:
            if fv[0] == "SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_GROUP_ID":
                assert table_group_id == fv[1]
            elif fv[0] == "SAI_ACL_TABLE_GROUP_MEMBER_ATTR_ACL_TABLE_ID":
                table_id = fv[1]
            elif fv[0] == "SAI_ACL_TABLE_GROUP_MEMBER_ATTR_PRIORITY":
                assert fv[1] == "100"
            else:
                assert False

        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_ACL_TABLE")
        (status, fvs) = tbl.get(table_id)
        assert status == True

    def check_asic_table_absent(self, dvs):
        tbl = swsscommon.Table(self.adb, "ASIC_STATE:SAI_OBJECT_TYPE_ACL_TABLE")
        acl_tables = tbl.getKeys()
        for key in dvs.asicdb.default_acl_tables:
            assert key in acl_tables
        acl_tables = [k for k in acl_tables if k not in dvs.asicdb.default_acl_tables]

        assert len(acl_tables) == 0

    # Frist create port channel
    # Second create ACL table
    def test_PortChannelAfterAcl(self, dvs):
        self.setup_db(dvs)
        dvs.runcmd("crm config polling interval 1")
        time.sleep(2)

        used_counter = dvs.getCrmCounterValue('ACL_STATS:INGRESS:LAG', 'crm_stats_acl_group_used')
        if used_counter is None:
            used_counter = 0
        # create port channel
        self.create_port_channel(dvs, "PortChannel01")

        # create ACL table
        self.create_acl_table(dvs, "LAG_ACL_TABLE", "PortChannel01")

        time.sleep(2)

        new_used_counter = dvs.getCrmCounterValue('ACL_STATS:INGRESS:LAG', 'crm_stats_acl_group_used')
        if new_used_counter is None:
            new_used_counter = 0
        assert new_used_counter - used_counter == 1
        # check ASIC table
        self.check_asic_table_existed(dvs)

        # remove ACL table
        self.remove_acl_table(dvs, "LAG_ACL_TABLE")

        # remove port channel
        self.remove_port_channel(dvs, "PortChannel01")

        time.sleep(2)
        new_new_used_counter = dvs.getCrmCounterValue('ACL_STATS:INGRESS:LAG', 'crm_stats_acl_group_used')
        if new_new_used_counter is None:
            new_new_used_counter = 0
        assert new_used_counter - new_new_used_counter == 1
        # slow down crm polling
        dvs.runcmd("crm config polling interval 10000")

    # Frist create ACL table
    # Second create port channel
    def test_PortChannelBeforeAcl(self, dvs):
        self.setup_db(dvs)

        # create ACL table
        self.create_acl_table(dvs, "LAG_ACL_TABLE", "PortChannel01")

        # create port channel
        self.create_port_channel(dvs, "PortChannel01")

        time.sleep(1)

        # check ASIC table
        self.check_asic_table_existed(dvs)

        # TODO: right now it is not supported to remove port before remove ACL
        # table. Will swap the order after having it supported
        # remove ACL table
        self.remove_acl_table(dvs, "LAG_ACL_TABLE")

        # remove port channel
        self.remove_port_channel(dvs, "PortChannel01")

    # ACL table cannot be created upon a member port of a port channel
    def test_AclOnPortChannelMember(self, dvs):
        self.setup_db(dvs)

        # create port channel
        self.create_port_channel(dvs, "PortChannel01")

        # add port channel member
        self.add_port_channel_members(dvs, "PortChannel01", ["Ethernet0", "Ethernet4"])
        self.activate_port_channel_members(dvs, "PortChannel01", ["Ethernet0", "Ethernet4"])

        # create ACL table
        self.create_acl_table(dvs, "LAG_ACL_TABLE", "Ethernet0")

        time.sleep(1)

        # check ASIC table
        self.check_asic_table_absent(dvs)

        # remove_acl_table
        self.remove_acl_table(dvs, "LAG_ACL_TABLE")

        # remove port channel member
        self.remove_port_channel_members(dvs, "PortChannel01", ["Ethernet0", "Ethernet4"])

        # remove port channel
        self.remove_port_channel(dvs, "PortChannel01")
