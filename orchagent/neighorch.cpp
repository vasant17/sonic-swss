#include <assert.h>
#include <inttypes.h>

#include "neighorch.h"
#include "logger.h"
#include "swssnet.h"
#include "crmorch.h"
#include "routeorch.h"

extern sai_neighbor_api_t*         sai_neighbor_api;
extern sai_next_hop_api_t*         sai_next_hop_api;

extern PortsOrch *gPortsOrch;
extern sai_object_id_t gSwitchId;
extern CrmOrch *gCrmOrch;
extern RouteOrch *gRouteOrch;

const int neighorch_pri = 30;

static bool send_message(struct nl_sock *socket_p, struct nl_msg *msg_p)
{
    int err = 0;

    if (!socket_p)
    {
        SWSS_LOG_ERROR("Netlink socket null pointer");
        return false;
    }

    if ((err = nl_send_auto(socket_p, msg_p)) < 0)
    {
        SWSS_LOG_ERROR("Netlink send message failed, error '%s'", nl_geterror(err));
        return false;
    }

    nlmsg_free(msg_p);
    return true;
}

NeighOrch::NeighOrch(DBConnector *db, string tableName, IntfsOrch *intfsOrch, FdbOrch *fdbOrch, PortsOrch *portsOrch) :
        Orch(db, tableName, neighorch_pri), m_intfsOrch(intfsOrch), m_fdbOrch(fdbOrch), m_portsOrch(portsOrch)
{
    int err = 0;

    SWSS_LOG_ENTER();

    m_fdbOrch->attach(this);
    gPortsOrch->attach(this);

    m_nl_sock = nl_socket_alloc();
    if (!m_nl_sock)
    {
        SWSS_LOG_ERROR("Netlink socket is NOT allocacted");
    }
    else if ((err = nl_connect(m_nl_sock, NETLINK_ROUTE)) < 0)
    {
        SWSS_LOG_ERROR("Netlink socket connect failed, error '%s'", nl_geterror(err));
        nl_socket_free(m_nl_sock);
        m_nl_sock = NULL;
    }
}

NeighOrch::~NeighOrch()
{
    if (m_fdbOrch)
    {
        m_fdbOrch->detach(this);
    }

    if (m_nl_sock)
    {
        nl_close(m_nl_sock);
        nl_socket_free(m_nl_sock);
    }
}

bool NeighOrch::flushNeighborEntry(const NeighborEntry &entry, const MacAddress &mac)
{
    SWSS_LOG_ENTER();

    IpAddress    ip = entry.ip_address;
    string       alias = entry.alias;

    SWSS_LOG_NOTICE("Flushing ARP entry '%s' as FDB entry '%s' is flushed",
                    ip.to_string().c_str(), mac.to_string().c_str());

    if (!m_nl_sock)
    {
        SWSS_LOG_ERROR("Netlink socket is NOT allocated");
        return false;
    }

    struct nl_msg *msg_p = nlmsg_alloc();
    if (!msg_p)
    {
        SWSS_LOG_ERROR("Netlink message alloc failed for '%s'", ip.to_string().c_str());
        return false;
    }

    auto flags = (NLM_F_REQUEST | NLM_F_ACK);
    struct nlmsghdr *hdr = nlmsg_put(msg_p, NL_AUTO_PORT, NL_AUTO_SEQ, RTM_DELNEIGH, 0, flags);

    if (!hdr)
    {
        SWSS_LOG_ERROR("Netlink message header alloc failed for '%s'", ip.to_string().c_str());
        nlmsg_free(msg_p);
        return false;
    }

    struct ndmsg *nd_msg_p = static_cast<struct ndmsg *>
                           (nlmsg_reserve(msg_p, sizeof(struct ndmsg), NLMSG_ALIGNTO));
    if (!nd_msg_p)
    {
        SWSS_LOG_ERROR("Netlink ndmsg reserve failed for '%s'", ip.to_string().c_str());
        nlmsg_free(msg_p);
        return false;
    }

    memset(nd_msg_p, 0, sizeof(struct ndmsg));

    nd_msg_p->ndm_ifindex = if_nametoindex(alias.c_str());

    // Fill in the IPV4/IPV6 address
    auto addr_len = ip.isV4()? sizeof(struct in_addr) : sizeof(struct in6_addr);

    struct rtattr *rta_p = static_cast<struct rtattr *>
                         (nlmsg_reserve(msg_p, sizeof(struct rtattr) + addr_len, NLMSG_ALIGNTO));
    if (!rta_p)
    {
        SWSS_LOG_ERROR("Netlink rtattr (IP) failed for '%s'", ip.to_string().c_str());
        nlmsg_free(msg_p);
        return false;
    }

    rta_p->rta_type = NDA_DST;
    rta_p->rta_len = static_cast<short>(RTA_LENGTH(addr_len));

    nd_msg_p->ndm_type = RTN_UNICAST;
    auto ip_addr = ip.getIp();

    if (ip.isV4())
    {
        nd_msg_p->ndm_family = AF_INET;
        memcpy(RTA_DATA(rta_p), &ip_addr.ip_addr.ipv4_addr, addr_len);
    }
    else
    {
        nd_msg_p->ndm_family = AF_INET6;
        memcpy(RTA_DATA(rta_p), &ip_addr.ip_addr.ipv6_addr, addr_len);
    }

    // Fill in the MAC address
    auto mac_len = ETHER_ADDR_LEN;
    auto mac_addr = mac.getMac();

    rta_p = static_cast<struct rtattr *>
          (nlmsg_reserve(msg_p, sizeof(struct rtattr) + mac_len, NLMSG_ALIGNTO));
    if (!rta_p)
    {
        SWSS_LOG_ERROR("Netlink rtattr (MAC) failed for '%s'", ip.to_string().c_str());
        nlmsg_free(msg_p);
        return false;
    }

    rta_p->rta_type = NDA_LLADDR;
    rta_p->rta_len = static_cast<short>(RTA_LENGTH(mac_len));
    memcpy(RTA_DATA(rta_p), mac_addr, mac_len);

    return send_message(m_nl_sock, msg_p);
}
/*
 * Function Name: processFDBUpdate
 * Description: Goal of this function is to delete neighbor/ARP entries
 *              when a port belonging to a VLAN gets deleted. 
 *              This function is called whenever neighbor orchagent receives
 *              SUBJECT_TYPE_FDB_CHANGE notification. Currently we only care for
 *              deleted FDB entries. We flush neighbor entry that matches its
 *              in-coming interface and MAC with FDB entry's VLAN name and MAC
 *              respectively. Also this ensures that underlying physical port is
 *              being deleted by checking its init status.
 * IN parameters: FdbUpdate
 * Returns: true if successfully flushes any ARP entry, false otherwise.
 */
bool NeighOrch::processFDBUpdate(const FdbUpdate& update)
{
    MacAddress fdbEntryMac = update.entry.mac;

    if (update.add)
    {
        // For now we are interested only in deleted FDB entries
        return true;
    }

    SWSS_LOG_NOTICE("Received FDB update, Flushing all ARP entries with matching MAC");

    // Get Vlan object
    Port vlan;
    if (!m_portsOrch->getPort(update.entry.bv_id, vlan))
    {
        SWSS_LOG_NOTICE("FdbOrch notification: Failed to locate vlan port from bv_id 0x%" PRIx64 ".", update.entry.bv_id);
        return false;
    }

    if (update.port.m_admin_state_up || update.port.m_oper_status == SAI_PORT_OPER_STATUS_UP)
    {
        
        SWSS_LOG_NOTICE("port %s is admin UP. Could be an AGED entry. Dont flush ARP.", update.port.m_alias.c_str());
        return false;
    }

    // If the FDB entry MAC matches with neighbor/ARP entry MAC,
    // and ARP entry incoming interface matches with VLAN name,
    // flush neighbor/arp entry.
    for (const auto &entry : m_syncdNeighbors)
    {
        if (entry.first.alias == vlan.m_alias && entry.second == fdbEntryMac)
        {
            return flushNeighborEntry(entry.first, entry.second);
        }
    }
    return true;
}

bool NeighOrch::processPortUpdate(const PortUpdate& update)
{
    if (update.add)
    {
        // Not interested in port add
        return true;
    }

    const Port &port = update.port;

    SWSS_LOG_NOTICE("Flushing all ARP entries resolved over interface %s",
                   port.m_alias.c_str());

    for (const auto &entry : m_syncdNeighbors)
    {
        SWSS_LOG_NOTICE("Looking at ARP entry <%s, %s>",
                        entry.first.ip_address.to_string().c_str(),
                        entry.first.alias.c_str());
        if (entry.first.alias == port.m_alias)
        {
            return flushNeighborEntry(entry.first, entry.second);
        }
    }
    return true;
   
}

void NeighOrch::update(SubjectType type, void *cntx)
{
    SWSS_LOG_ENTER();

    assert(cntx);

    switch(type) {
        case SUBJECT_TYPE_FDB_CHANGE:
        {
            FdbUpdate *update = reinterpret_cast<FdbUpdate *>(cntx);
            processFDBUpdate(*update);
            break;
        }
        case SUBJECT_TYPE_PORT_CHANGE:
        {
            PortUpdate *update = reinterpret_cast<PortUpdate *>(cntx);
            processPortUpdate(*update);
            break;
        }
        default:
            break;
    }

    return;
}

bool NeighOrch::hasNextHop(const NextHopKey &nexthop)
{
    return m_syncdNextHops.find(nexthop) != m_syncdNextHops.end();
}

bool NeighOrch::addNextHop(const IpAddress &ipAddress, const string &alias)
{
    SWSS_LOG_ENTER();

    Port p;
    if (!gPortsOrch->getPort(alias, p))
    {
        SWSS_LOG_ERROR("Neighbor %s seen on port %s which doesn't exist",
                        ipAddress.to_string().c_str(), alias.c_str());
        return false;
    }

    NextHopKey nexthop = { ipAddress, alias };
    assert(!hasNextHop(nexthop));
    sai_object_id_t rif_id = m_intfsOrch->getRouterIntfsId(alias);

    vector<sai_attribute_t> next_hop_attrs;

    sai_attribute_t next_hop_attr;
    next_hop_attr.id = SAI_NEXT_HOP_ATTR_TYPE;
    next_hop_attr.value.s32 = SAI_NEXT_HOP_TYPE_IP;
    next_hop_attrs.push_back(next_hop_attr);

    next_hop_attr.id = SAI_NEXT_HOP_ATTR_IP;
    copy(next_hop_attr.value.ipaddr, ipAddress);
    next_hop_attrs.push_back(next_hop_attr);

    next_hop_attr.id = SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID;
    next_hop_attr.value.oid = rif_id;
    next_hop_attrs.push_back(next_hop_attr);

    sai_object_id_t next_hop_id;
    sai_status_t status = sai_next_hop_api->create_next_hop(&next_hop_id, gSwitchId, (uint32_t)next_hop_attrs.size(), next_hop_attrs.data());
    if (status != SAI_STATUS_SUCCESS)
    {
        SWSS_LOG_ERROR("Failed to create next hop %s on %s, rv:%d",
                       ipAddress.to_string().c_str(), alias.c_str(), status);
        return false;
    }

    SWSS_LOG_NOTICE("Created next hop %s on %s",
                    ipAddress.to_string().c_str(), alias.c_str());

    NextHopEntry next_hop_entry;
    next_hop_entry.next_hop_id = next_hop_id;
    next_hop_entry.ref_count = 0;
    next_hop_entry.nh_flags = 0;
    m_syncdNextHops[nexthop] = next_hop_entry;

    m_intfsOrch->increaseRouterIntfsRefCount(alias);

    if (ipAddress.isV4())
    {
        gCrmOrch->incCrmResUsedCounter(CrmResourceType::CRM_IPV4_NEXTHOP);
    }
    else
    {
        gCrmOrch->incCrmResUsedCounter(CrmResourceType::CRM_IPV6_NEXTHOP);
    }

    // For nexthop with incoming port which has down oper status, NHFLAGS_IFDOWN
    // flag Should be set on it.
    // This scenario may happen under race condition where buffered neighbor event
    // is processed after incoming port is down.
    if (p.m_oper_status == SAI_PORT_OPER_STATUS_DOWN)
    {
        if (setNextHopFlag(nexthop, NHFLAGS_IFDOWN) == false)
        {
            SWSS_LOG_WARN("Failed to set NHFLAGS_IFDOWN on nexthop %s for interface %s",
                ipAddress.to_string().c_str(), alias.c_str());
        }
    }
    return true;
}

bool NeighOrch::setNextHopFlag(const NextHopKey &nexthop, const uint32_t nh_flag)
{
    SWSS_LOG_ENTER();

    auto nhop = m_syncdNextHops.find(nexthop);
    bool rc = false;

    assert(nhop != m_syncdNextHops.end());

    if (nhop->second.nh_flags & nh_flag)
    {
        return true;
    }

    nhop->second.nh_flags |= nh_flag;

    switch (nh_flag)
    {
        case NHFLAGS_IFDOWN:
            rc = gRouteOrch->invalidnexthopinNextHopGroup(nexthop);
            break;
        default:
            assert(0);
            break;
    }

    return rc;
}

bool NeighOrch::clearNextHopFlag(const NextHopKey &nexthop, const uint32_t nh_flag)
{
    SWSS_LOG_ENTER();

    auto nhop = m_syncdNextHops.find(nexthop);
    bool rc = false;

    assert(nhop != m_syncdNextHops.end());

    if (!(nhop->second.nh_flags & nh_flag))
    {
        return true;
    }

    nhop->second.nh_flags &= ~nh_flag;

    switch (nh_flag)
    {
        case NHFLAGS_IFDOWN:
            rc = gRouteOrch->validnexthopinNextHopGroup(nexthop);
            break;
        default:
            assert(0);
            break;
    }

    return rc;
}

bool NeighOrch::isNextHopFlagSet(const NextHopKey &nexthop, const uint32_t nh_flag)
{
    SWSS_LOG_ENTER();

    auto nhop = m_syncdNextHops.find(nexthop);

    assert(nhop != m_syncdNextHops.end());

    if (nhop->second.nh_flags & nh_flag)
    {
        return true;
    }

    return false;
}

bool NeighOrch::ifChangeInformNextHop(const string &alias, bool if_up)
{
    SWSS_LOG_ENTER();
    bool rc = true;

    for (auto nhop = m_syncdNextHops.begin(); nhop != m_syncdNextHops.end(); ++nhop)
    {
        if (nhop->first.alias != alias)
        {
            continue;
        }

        if (if_up)
        {
            rc = clearNextHopFlag(nhop->first, NHFLAGS_IFDOWN);
        }
        else
        {
            rc = setNextHopFlag(nhop->first, NHFLAGS_IFDOWN);
        }

        if (rc == true)
        {
            continue;
        }
        else
        {
            break;
        }
    }

    return rc;
}

bool NeighOrch::removeNextHop(const IpAddress &ipAddress, const string &alias)
{
    SWSS_LOG_ENTER();

    NextHopKey nexthop = { ipAddress, alias };
    assert(hasNextHop(nexthop));

    if (m_syncdNextHops[nexthop].ref_count > 0)
    {
        SWSS_LOG_ERROR("Failed to remove still referenced next hop %s on %s",
                       ipAddress.to_string().c_str(), alias.c_str());
        return false;
    }

    m_syncdNextHops.erase(nexthop);
    m_intfsOrch->decreaseRouterIntfsRefCount(alias);
    return true;
}

sai_object_id_t NeighOrch::getNextHopId(const NextHopKey &nexthop)
{
    assert(hasNextHop(nexthop));
    return m_syncdNextHops[nexthop].next_hop_id;
}

int NeighOrch::getNextHopRefCount(const NextHopKey &nexthop)
{
    assert(hasNextHop(nexthop));
    return m_syncdNextHops[nexthop].ref_count;
}

void NeighOrch::increaseNextHopRefCount(const NextHopKey &nexthop)
{
    assert(hasNextHop(nexthop));
    m_syncdNextHops[nexthop].ref_count ++;
}

void NeighOrch::decreaseNextHopRefCount(const NextHopKey &nexthop)
{
    assert(hasNextHop(nexthop));
    m_syncdNextHops[nexthop].ref_count --;
}

bool NeighOrch::getNeighborEntry(const NextHopKey &nexthop, NeighborEntry &neighborEntry, MacAddress &macAddress)
{
    if (!hasNextHop(nexthop))
    {
        return false;
    }

    for (const auto &entry : m_syncdNeighbors)
    {
        if (entry.first.ip_address == nexthop.ip_address && entry.first.alias == nexthop.alias)
        {
            neighborEntry = entry.first;
            macAddress = entry.second;
            return true;
        }
    }

    return false;
}

bool NeighOrch::getNeighborEntry(const IpAddress &ipAddress, NeighborEntry &neighborEntry, MacAddress &macAddress)
{
    string alias = m_intfsOrch->getRouterIntfsAlias(ipAddress);
    if (alias.empty())
    {
        return false;
    }

    NextHopKey nexthop(ipAddress, alias);
    return getNeighborEntry(nexthop, neighborEntry, macAddress);
}

void NeighOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    if (!gPortsOrch->allPortsReady())
    {
        return;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;

        string key = kfvKey(t);
        string op = kfvOp(t);

        size_t found = key.find(':');
        if (found == string::npos)
        {
            SWSS_LOG_ERROR("Failed to parse key %s", key.c_str());
            it = consumer.m_toSync.erase(it);
            continue;
        }

        string alias = key.substr(0, found);

        if (alias == "eth0" || alias == "lo" || alias == "docker0")
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        IpAddress ip_address(key.substr(found+1));

        NeighborEntry neighbor_entry = { ip_address, alias };

        if (op == SET_COMMAND)
        {
            Port p;
            if (!gPortsOrch->getPort(alias, p))
            {
                SWSS_LOG_INFO("Port %s doesn't exist", alias.c_str());
                it++;
                continue;
            }

            if (!p.m_rif_id)
            {
                SWSS_LOG_INFO("Router interface doesn't exist on %s", alias.c_str());
                it++;
                continue;
            }

            MacAddress mac_address;
            for (auto i = kfvFieldsValues(t).begin();
                 i  != kfvFieldsValues(t).end(); i++)
            {
                if (fvField(*i) == "neigh")
                    mac_address = MacAddress(fvValue(*i));
            }

            if (m_syncdNeighbors.find(neighbor_entry) == m_syncdNeighbors.end() || m_syncdNeighbors[neighbor_entry] != mac_address)
            {
                if (addNeighbor(neighbor_entry, mac_address))
                    it = consumer.m_toSync.erase(it);
                else
                    it++;
            }
            else
                /* Duplicate entry */
                it = consumer.m_toSync.erase(it);
        }
        else if (op == DEL_COMMAND)
        {
            if (m_syncdNeighbors.find(neighbor_entry) != m_syncdNeighbors.end())
            {
                if (removeNeighbor(neighbor_entry))
                {
                    it = consumer.m_toSync.erase(it);
                }
                else
                {
                    it++;
                }
            }
            else
                /* Cannot locate the neighbor */
                it = consumer.m_toSync.erase(it);
        }
        else
        {
            SWSS_LOG_ERROR("Unknown operation type %s", op.c_str());
            it = consumer.m_toSync.erase(it);
        }
    }
}

bool NeighOrch::addNeighbor(const NeighborEntry &neighborEntry, const MacAddress &macAddress)
{
    SWSS_LOG_ENTER();

    sai_status_t status;
    IpAddress ip_address = neighborEntry.ip_address;
    string alias = neighborEntry.alias;

    sai_object_id_t rif_id = m_intfsOrch->getRouterIntfsId(alias);
    if (rif_id == SAI_NULL_OBJECT_ID)
    {
        SWSS_LOG_INFO("Failed to get rif_id for %s", alias.c_str());
        return false;
    }

    sai_neighbor_entry_t neighbor_entry;
    neighbor_entry.rif_id = rif_id;
    neighbor_entry.switch_id = gSwitchId;
    copy(neighbor_entry.ip_address, ip_address);

    sai_attribute_t neighbor_attr;
    neighbor_attr.id = SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS;
    memcpy(neighbor_attr.value.mac, macAddress.getMac(), 6);

    if (m_syncdNeighbors.find(neighborEntry) == m_syncdNeighbors.end())
    {
        status = sai_neighbor_api->create_neighbor_entry(&neighbor_entry, 1, &neighbor_attr);
        if (status != SAI_STATUS_SUCCESS)
        {
            if (status == SAI_STATUS_ITEM_ALREADY_EXISTS)
            {
                SWSS_LOG_ERROR("Entry exists: neighbor %s on %s, rv:%d",
                           macAddress.to_string().c_str(), alias.c_str(), status);
                /* Returning True so as to skip retry */
                return true;
            }
            else
            {
                SWSS_LOG_ERROR("Failed to create neighbor %s on %s, rv:%d",
                           macAddress.to_string().c_str(), alias.c_str(), status);
                return false;
            }
        }

        SWSS_LOG_NOTICE("Created neighbor %s on %s", macAddress.to_string().c_str(), alias.c_str());
        m_intfsOrch->increaseRouterIntfsRefCount(alias);

        if (neighbor_entry.ip_address.addr_family == SAI_IP_ADDR_FAMILY_IPV4)
        {
            gCrmOrch->incCrmResUsedCounter(CrmResourceType::CRM_IPV4_NEIGHBOR);
        }
        else
        {
            gCrmOrch->incCrmResUsedCounter(CrmResourceType::CRM_IPV6_NEIGHBOR);
        }

        if (!addNextHop(ip_address, alias))
        {
            status = sai_neighbor_api->remove_neighbor_entry(&neighbor_entry);
            if (status != SAI_STATUS_SUCCESS)
            {
                SWSS_LOG_ERROR("Failed to remove neighbor %s on %s, rv:%d",
                               macAddress.to_string().c_str(), alias.c_str(), status);
                return false;
            }
            m_intfsOrch->decreaseRouterIntfsRefCount(alias);

            if (neighbor_entry.ip_address.addr_family == SAI_IP_ADDR_FAMILY_IPV4)
            {
                gCrmOrch->decCrmResUsedCounter(CrmResourceType::CRM_IPV4_NEIGHBOR);
            }
            else
            {
                gCrmOrch->decCrmResUsedCounter(CrmResourceType::CRM_IPV6_NEIGHBOR);
            }

            return false;
        }
    }
    else
    {
        status = sai_neighbor_api->set_neighbor_entry_attribute(&neighbor_entry, &neighbor_attr);
        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_ERROR("Failed to update neighbor %s on %s, rv:%d",
                           macAddress.to_string().c_str(), alias.c_str(), status);
            return false;
        }
        SWSS_LOG_NOTICE("Updated neighbor %s on %s", macAddress.to_string().c_str(), alias.c_str());
    }

    m_syncdNeighbors[neighborEntry] = macAddress;

    NeighborUpdate update = { neighborEntry, macAddress, true };
    notify(SUBJECT_TYPE_NEIGH_CHANGE, static_cast<void *>(&update));

    return true;
}

bool NeighOrch::removeNeighbor(const NeighborEntry &neighborEntry)
{
    SWSS_LOG_ENTER();

    sai_status_t status;
    IpAddress ip_address = neighborEntry.ip_address;
    string alias = neighborEntry.alias;
    NextHopKey nexthop = { ip_address, alias };

    if (m_syncdNeighbors.find(neighborEntry) == m_syncdNeighbors.end())
    {
        return true;
    }

    if (m_syncdNextHops[nexthop].ref_count > 0)
    {
        SWSS_LOG_INFO("Failed to remove still referenced neighbor %s on %s",
                      m_syncdNeighbors[neighborEntry].to_string().c_str(), alias.c_str());
        return false;
    }

    sai_object_id_t rif_id = m_intfsOrch->getRouterIntfsId(alias);

    sai_neighbor_entry_t neighbor_entry;
    neighbor_entry.rif_id = rif_id;
    neighbor_entry.switch_id = gSwitchId;
    copy(neighbor_entry.ip_address, ip_address);

    sai_object_id_t next_hop_id = m_syncdNextHops[nexthop].next_hop_id;
    status = sai_next_hop_api->remove_next_hop(next_hop_id);
    if (status != SAI_STATUS_SUCCESS)
    {
        /* When next hop is not found, we continue to remove neighbor entry. */
        if (status == SAI_STATUS_ITEM_NOT_FOUND)
        {
            SWSS_LOG_ERROR("Failed to locate next hop %s on %s, rv:%d",
                           ip_address.to_string().c_str(), alias.c_str(), status);
        }
        else
        {
            SWSS_LOG_ERROR("Failed to remove next hop %s on %s, rv:%d",
                           ip_address.to_string().c_str(), alias.c_str(), status);
            return false;
        }
    }

    if (status != SAI_STATUS_ITEM_NOT_FOUND)
    {
        if (neighbor_entry.ip_address.addr_family == SAI_IP_ADDR_FAMILY_IPV4)
        {
            gCrmOrch->decCrmResUsedCounter(CrmResourceType::CRM_IPV4_NEXTHOP);
        }
        else
        {
            gCrmOrch->decCrmResUsedCounter(CrmResourceType::CRM_IPV6_NEXTHOP);
        }
    }

    SWSS_LOG_NOTICE("Removed next hop %s on %s",
                    ip_address.to_string().c_str(), alias.c_str());

    status = sai_neighbor_api->remove_neighbor_entry(&neighbor_entry);
    if (status != SAI_STATUS_SUCCESS)
    {
        if (status == SAI_STATUS_ITEM_NOT_FOUND)
        {
            SWSS_LOG_ERROR("Failed to locate neigbor %s on %s, rv:%d",
                    m_syncdNeighbors[neighborEntry].to_string().c_str(), alias.c_str(), status);
            return true;
        }
        else
        {
            SWSS_LOG_ERROR("Failed to remove neighbor %s on %s, rv:%d",
                    m_syncdNeighbors[neighborEntry].to_string().c_str(), alias.c_str(), status);
            return false;
        }
    }

    if (neighbor_entry.ip_address.addr_family == SAI_IP_ADDR_FAMILY_IPV4)
    {
        gCrmOrch->decCrmResUsedCounter(CrmResourceType::CRM_IPV4_NEIGHBOR);
    }
    else
    {
        gCrmOrch->decCrmResUsedCounter(CrmResourceType::CRM_IPV6_NEIGHBOR);
    }

    SWSS_LOG_NOTICE("Removed neighbor %s on %s",
            m_syncdNeighbors[neighborEntry].to_string().c_str(), alias.c_str());

    m_syncdNeighbors.erase(neighborEntry);
    m_intfsOrch->decreaseRouterIntfsRefCount(alias);

    NeighborUpdate update = { neighborEntry, MacAddress(), false };
    notify(SUBJECT_TYPE_NEIGH_CHANGE, static_cast<void *>(&update));

    removeNextHop(ip_address, alias);

    return true;
}
